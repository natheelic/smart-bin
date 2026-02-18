import { createClient } from '@supabase/supabase-js';
import jwt from 'jsonwebtoken';

const supabase = createClient(
  process.env.SUPABASE_URL,
  process.env.SUPABASE_SERVICE_ROLE_KEY
);

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
//  GET /api/device  — Browser ดึงสถานะทั้งหมด
// ============================================================
async function handleGet(res) {
  const [deviceRes, sensorsRes, servosRes] = await Promise.all([
    supabase.from('pico_device').select('*').eq('id', 1).single(),
    supabase.from('ultrasonic_sensors').select('*').order('id'),
    supabase.from('servos').select('*').order('id'),
  ]);

  if (deviceRes.error)  return res.status(500).json({ error: deviceRes.error.message });
  if (sensorsRes.error) return res.status(500).json({ error: sensorsRes.error.message });
  if (servosRes.error)  return res.status(500).json({ error: servosRes.error.message });

  return res.status(200).json({
    device:  deviceRes.data,
    sensors: sensorsRes.data,
    servos:  servosRes.data,
  });
}

// ============================================================
//  POST /api/device  — จาก Browser (from_app: true)
// ============================================================
async function handleAppPost(body, res) {
  const { action, payload } = body;

  // สลับโหมด: { action:'set_mode', payload:'auto'|'manual' }
  if (action === 'set_mode') {
    const { error } = await supabase
      .from('pico_device')
      .update({ mode: payload, updated_at: new Date() })
      .eq('id', 1);
    if (error) return res.status(500).json({ error: error.message });
    return res.status(200).json({ success: true });
  }

  // ตั้ง threshold ของ sensor: { action:'set_threshold', payload:{ id:1, threshold_cm:80 } }
  if (action === 'set_threshold') {
    const { id, threshold_cm } = payload;
    const { error } = await supabase
      .from('ultrasonic_sensors')
      .update({ threshold_cm, updated_at: new Date() })
      .eq('id', id);
    if (error) return res.status(500).json({ error: error.message });
    return res.status(200).json({ success: true });
  }

  // สั่ง servo (manual): { action:'set_servo', payload:{ id:1, command_angle:90, enabled:true } }
  if (action === 'set_servo') {
    const { id, command_angle, enabled } = payload;
    const update = { updated_at: new Date() };
    if (command_angle !== undefined) update.command_angle = command_angle;
    if (enabled       !== undefined) update.enabled       = enabled;
    const { error } = await supabase
      .from('servos')
      .update(update)
      .eq('id', id);
    if (error) return res.status(500).json({ error: error.message });
    return res.status(200).json({ success: true });
  }

  return res.status(400).json({ error: 'Unknown action' });
}

// ============================================================
//  POST /api/device  — จาก Pico WH (x-device-secret header)
//  Body: { sensors:[{id,distance_cm},...] }
//  Response: { mode, thresholds:[{id,threshold_cm}], servos:[{id,command_angle,enabled}] }
// ============================================================
async function handleDevicePost(body, res) {
  const now = new Date();

  // อัปเดตระยะทางจาก sensor ทุกตัว
  if (body.sensors?.length) {
    await Promise.all(body.sensors.map(s =>
      supabase.from('ultrasonic_sensors')
        .update({ distance_cm: s.distance_cm, updated_at: now })
        .eq('id', s.id)
    ));
  }

  // อัปเดต device_last_seen
  await supabase.from('pico_device')
    .update({ device_last_seen: now })
    .eq('id', 1);

  // ดึงคำสั่งกลับ
  const [deviceRes, sensorsRes, servosRes] = await Promise.all([
    supabase.from('pico_device').select('mode').eq('id', 1).single(),
    supabase.from('ultrasonic_sensors').select('id, threshold_cm').order('id'),
    supabase.from('servos').select('id, command_angle, enabled').order('id'),
  ]);

  return res.status(200).json({
    mode:       deviceRes.data.mode,
    thresholds: sensorsRes.data,
    servos:     servosRes.data,
  });
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
