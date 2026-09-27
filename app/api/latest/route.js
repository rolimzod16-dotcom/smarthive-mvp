import { NextResponse } from 'next/server';
import { getLatest, storageMode } from '../../../lib/store';

export const dynamic = 'force-dynamic';

export async function GET() {
  return NextResponse.json({ ok: true, data: await getLatest(), storage: storageMode() });
}

