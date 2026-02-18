import jwt from 'jsonwebtoken';

export default function handler(req, res) {
  if (req.method !== 'POST') return res.status(405).end();

  const { password } = req.body;
  const correctPassword = process.env.ACCESS_PASSWORD;
  const jwtSecret = process.env.JWT_SECRET;

  if (!correctPassword || !jwtSecret) {
    return res.status(500).json({ error: 'Server not configured: ACCESS_PASSWORD or JWT_SECRET missing' });
  }

  if (password !== correctPassword) {
    return res.status(401).json({ error: 'รหัสผ่านไม่ถูกต้อง' });
  }

  const token = jwt.sign({ authorized: true }, jwtSecret, { expiresIn: '24h' });
  return res.status(200).json({ token });
}
