const memory = globalThis.__smartHiveMemory || { latest: null, history: [] };
globalThis.__smartHiveMemory = memory;

function redisConfig() {
  return {
    url: process.env.KV_REST_API_URL || process.env.UPSTASH_REDIS_REST_URL,
    token: process.env.KV_REST_API_TOKEN || process.env.UPSTASH_REDIS_REST_TOKEN,
  };
}

async function redis(command) {
  const { url, token } = redisConfig();
  if (!url || !token) return null;
  const response = await fetch(`${url}/${command.map(encodeURIComponent).join('/')}`, {
    headers: { Authorization: `Bearer ${token}` },
    cache: 'no-store',
  });
  if (!response.ok) throw new Error(`Redis request failed: ${response.status}`);
  return (await response.json()).result;
}

export async function saveTelemetry(item) {
  const serialized = JSON.stringify(item);
  const configured = redisConfig().url && redisConfig().token;
  if (configured) {
    await redis(['SET', `smarthive:latest:${item.deviceId}`, serialized]);
    await redis(['SET', 'smarthive:latest-device', item.deviceId]);
    await redis(['LPUSH', `smarthive:history:${item.deviceId}`, serialized]);
    await redis(['LTRIM', `smarthive:history:${item.deviceId}`, '0', '499']);
    return;
  }
  memory.latest = item;
  memory.history.unshift(item);
  memory.history = memory.history.slice(0, 500);
}

export async function getLatest() {
  const configured = redisConfig().url && redisConfig().token;
  if (!configured) return memory.latest;
  const deviceId = await redis(['GET', 'smarthive:latest-device']);
  if (!deviceId) return null;
  const raw = await redis(['GET', `smarthive:latest:${deviceId}`]);
  return raw ? JSON.parse(raw) : null;
}

export async function getHistory(limit = 100) {
  const safeLimit = Math.max(1, Math.min(Number(limit) || 100, 500));
  const configured = redisConfig().url && redisConfig().token;
  if (!configured) return memory.history.slice(0, safeLimit);
  const deviceId = await redis(['GET', 'smarthive:latest-device']);
  if (!deviceId) return [];
  const rows = await redis(['LRANGE', `smarthive:history:${deviceId}`, '0', String(safeLimit - 1)]);
  return (rows || []).map((row) => JSON.parse(row));
}

export function storageMode() {
  return redisConfig().url && redisConfig().token ? 'persistent' : 'temporary';
}

