import { NextResponse } from 'next/server';
import { storageMode } from '../../../lib/store';

export const dynamic = 'force-dynamic';

export async function GET() {
  return NextResponse.json({ ok: true, service: 'SmartHive API', storage: storageMode() });
}

