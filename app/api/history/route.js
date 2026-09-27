import { NextResponse } from 'next/server';
import { getHistory } from '../../../lib/store';

export const dynamic = 'force-dynamic';

export async function GET(request) {
  const limit = new URL(request.url).searchParams.get('limit');
  return NextResponse.json({ ok: true, data: await getHistory(limit) });
}

