import { createClient } from '@supabase/supabase-js';
import jwt from 'jsonwebtoken';

const supabase = createClient(
  process.env.NEXT_PUBLIC_SUPABASE_URL,
  process.env.SUPABASE_SERVICE_ROLE_KEY
);

/* ── helpers ── */
function verifyAppToken(req) {
  const auth = req.headers['authorization'];
  if (!auth?.startsWith('Bearer ')) return false;
  try { jwt.verify(auth.split(' ')[1], process.env.JWT_SECRET); return true; }
  catch { return false; }
}
function isDeviceRequest(req) {
  return req.headers['x-device-secret'] === process.env.DEVICE_SECRET;
}

// ============================================================
//  GET  /api/device — Dashboard ดึงสถานะถังทั้ง 4 ใบ
// ============================================================
async function handleGet(res) {
  const { data: bins, error } = await supabase
    .from('bins')
    .select('*')
    .order('id');
  if (error) return res.status(500).json({ error: error.message });

  const { data: logs } = await supabase
    .from('bin_logs')
    .select('*')
    .order('created_at', { ascending: false })
    .limit(30);

  return res.status(200).json({ bins, logs: logs || [] });
}

// ============================================================
//  POST /api/device  — จาก Dashboard (from_app: true)
// ============================================================
async function handleAppPost(body, res) {
  const { action, payload } = body;
  const now = new Date().toISOString();

  // ─── เปลี่ยน Mode ถังเดียว ───
  // { action:'set_mode', payload:{ id:1, mode:'manual' } }
  if (action === 'set_mode') {
    const { id, mode } = payload;
    if (!['auto', 'manual'].includes(mode))
      return res.status(400).json({ error: 'mode ต้องเป็น auto หรือ manual' });

    const update = { mode, updated_at: now };
    if (mode === 'auto') update.manual_open = false;          // รีเซ็ตคำสั่ง manual

    const { error } = await supabase.from('bins').update(update).eq('id', id);
    if (error) return res.status(500).json({ error: error.message });
    await logAction(id, 'mode_changed', `เปลี่ยนเป็น ${mode}`);
    return res.status(200).json({ success: true });
  }

  // ─── เปลี่ยน Mode ทุกถัง ───
  // { action:'set_all_mode', payload:'auto'|'manual' }
  if (action === 'set_all_mode') {
    const mode = payload;
    if (!['auto', 'manual'].includes(mode))
      return res.status(400).json({ error: 'mode ต้องเป็น auto หรือ manual' });

    const update = { mode, updated_at: now };
    if (mode === 'auto') update.manual_open = false;

    const { error } = await supabase.from('bins').update(update).gte('id', 1);
    if (error) return res.status(500).json({ error: error.message });
    for (let i = 1; i <= 4; i++) await logAction(i, 'mode_changed', `ทั้งหมดเปลี่ยนเป็น ${mode}`);
    return res.status(200).json({ success: true });
  }

  // ─── ตั้ง Threshold ───
  // { action:'set_threshold', payload:{ id:1, threshold_cm:30 } }
  if (action === 'set_threshold') {
    const { id, threshold_cm } = payload;
    if (typeof threshold_cm !== 'number' || threshold_cm < 5 || threshold_cm > 200)
      return res.status(400).json({ error: 'threshold ต้องอยู่ระหว่าง 5-200 cm' });

    const { error } = await supabase
      .from('bins').update({ threshold_cm, updated_at: now }).eq('id', id);
    if (error) return res.status(500).json({ error: error.message });
    await logAction(id, 'threshold_changed', `ตั้งเป็น ${threshold_cm} cm`);
    return res.status(200).json({ success: true });
  }

  // ─── Manual เปิด/ปิดฝา ───
  // { action:'manual_lid', payload:{ id:1, open:true } }
  if (action === 'manual_lid') {
    const { id, open } = payload;

    // ตรวจสอบ manual mode
    const { data: bin } = await supabase.from('bins').select('mode').eq('id', id).single();
    if (!bin) return res.status(404).json({ error: 'ไม่พบถัง' });
    if (bin.mode !== 'manual')
      return res.status(400).json({ error: 'ถังไม่ได้อยู่ในโหมด manual' });

    const { error } = await supabase
      .from('bins').update({ manual_open: !!open, updated_at: now }).eq('id', id);
    if (error) return res.status(500).json({ error: error.message });
    await logAction(id, open ? 'manual_open' : 'manual_close', 'Manual control');
    return res.status(200).json({ success: true });
  }

  return res.status(400).json({ error: 'Unknown action' });
}

// ============================================================
//  POST /api/device  — จาก Pico WH  (x-device-secret)
//  Body : { bins:[{ id, distance_cm, lid_open }] }
//  Reply: { bins:[{ id, mode, threshold_cm, manual_open }] }
// ============================================================
async function handleDevicePost(body, res) {
  const now = new Date().toISOString();

  // อัปเดตค่าจาก sensor
  if (Array.isArray(body.bins)) {
    for (const b of body.bins) {
      const lid_status = b.lid_open ? 'open' : 'closed';
      // ดึงสถานะเดิม เพื่อ log เมื่อเปลี่ยน
      const { data: cur } = await supabase
        .from('bins').select('lid_status').eq('id', b.id).single();

      await supabase.from('bins').update({
        distance_cm: b.distance_cm,
        lid_status,
        device_last_seen: now,
        updated_at: now,
      }).eq('id', b.id);

      if (cur && cur.lid_status !== lid_status) {
        await logAction(b.id,
          lid_status === 'open' ? 'lid_opened' : 'lid_closed',
          `ระยะ ${b.distance_cm} cm`);
      }
    }
  }

  // ส่งคำสั่งกลับ
  const { data: bins } = await supabase
    .from('bins')
    .select('id, mode, threshold_cm, manual_open')
    .order('id');

  return res.status(200).json({ bins: bins || [] });
}

// ============================================================
//  Helper — เขียน Log
// ============================================================
async function logAction(bin_id, action, detail) {
  await supabase.from('bin_logs').insert({ bin_id, action, detail });
}

// ============================================================
//  MAIN HANDLER
// ============================================================
export default async function handler(req, res) {
  if (req.method === 'GET') {
    if (!verifyAppToken(req)) return res.status(401).json({ error: 'Unauthorized' });
    return handleGet(res);
  }

  if (req.method === 'POST') {
    if (req.body?.from_app) {
      if (!verifyAppToken(req)) return res.status(401).json({ error: 'Unauthorized' });
      return handleAppPost(req.body, res);
    }
    if (isDeviceRequest(req)) return handleDevicePost(req.body, res);
    return res.status(401).json({ error: 'Unauthorized' });
  }

  return res.status(405).end();
}
