import { NextResponse } from 'next/server';
import { saveTelemetry, storageMode } from '../../../lib/store';

export const dynamic = 'force-dynamic';

export async function POST(request) {
  const expectedKey = process.env.DEVICE_API_KEY;
  if (expectedKey && request.headers.get('x-api-key') !== expectedKey) {
    return NextResponse.json({ ok: false, error: 'Unauthorized' }, { status: 401 });
  }

  let body;
  try {
    body = await request.json();
  } catch {
    return NextResponse.json({ ok: false, error: 'Invalid JSON' }, { status: 400 });
  }
  if (!body.deviceId || typeof body.deviceId !== 'string') {
    return NextResponse.json({ ok: false, error: 'deviceId is required' }, { status: 400 });
  }

  const item = {
    ...body,
    deviceId: body.deviceId.slice(0, 64),
    receivedAt: new Date().toISOString(),
  };
  await saveTelemetry(item);
  return NextResponse.json({ ok: true, receivedAt: item.receivedAt, storage: storageMode() });
}

