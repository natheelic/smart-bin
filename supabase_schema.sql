-- ═══════════════════════════════════════════════════════════
--  Smart Trash Bin — Supabase SQL
--  รันใน Supabase → SQL Editor
-- ═══════════════════════════════════════════════════════════

-- ลบตารางเก่า (ถ้ามี)
DROP TABLE IF EXISTS bin_logs;
DROP TABLE IF EXISTS bins;

-- ─── ตาราง bins: ถังขยะ 4 ใบ ───
CREATE TABLE bins (
  id               INTEGER PRIMARY KEY,
  name             TEXT NOT NULL,
  mode             TEXT NOT NULL DEFAULT 'auto'
                     CHECK (mode IN ('auto', 'manual')),
  threshold_cm     REAL NOT NULL DEFAULT 30.0,
  distance_cm      REAL NOT NULL DEFAULT 999.0,
  lid_status       TEXT NOT NULL DEFAULT 'closed'
                     CHECK (lid_status IN ('open', 'closed')),
  manual_open      BOOLEAN NOT NULL DEFAULT FALSE,
  device_last_seen TIMESTAMPTZ,
  updated_at       TIMESTAMPTZ DEFAULT NOW()
);

-- ─── ตาราง bin_logs: ประวัติการทำงาน ───
CREATE TABLE bin_logs (
  id         BIGSERIAL PRIMARY KEY,
  bin_id     INTEGER NOT NULL REFERENCES bins(id),
  action     TEXT NOT NULL,
  detail     TEXT,
  created_at TIMESTAMPTZ DEFAULT NOW()
);

-- ─── Index สำหรับ query logs ───
CREATE INDEX idx_bin_logs_created ON bin_logs (created_at DESC);
CREATE INDEX idx_bin_logs_bin_id  ON bin_logs (bin_id);

-- ─── ใส่ข้อมูลเริ่มต้น 4 ถัง ───
INSERT INTO bins (id, name, threshold_cm) VALUES
  (1, 'ถังทั่วไป',    30),
  (2, 'ถังรีไซเคิล',  30),
  (3, 'ถังอันตราย',   30),
  (4, 'ถังอินทรีย์',   30);

-- ═══════════════════════════════════════════════════════════
--  (Optional) เปิด Realtime สำหรับ bins
-- ═══════════════════════════════════════════════════════════
-- ALTER PUBLICATION supabase_realtime ADD TABLE bins;
