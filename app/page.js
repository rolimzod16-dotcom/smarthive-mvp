'use client';

import { useEffect, useMemo, useState } from 'react';

function Card({ label, value, unit, ok = true }) {
  return (
    <article className="card">
      <span>{label}</span>
      <strong className={ok ? '' : 'bad'}>{value ?? '—'}{value != null && unit ? <small> {unit}</small> : null}</strong>
    </article>
  );
}

function yesNo(value) {
  if (value == null) return '—';
  return value ? 'OK' : 'Ошибка';
}

export default function Dashboard() {
  const [latest, setLatest] = useState(null);
  const [history, setHistory] = useState([]);
  const [storage, setStorage] = useState('unknown');
  const [error, setError] = useState('');

  async function refresh() {
    try {
      const [latestResponse, historyResponse] = await Promise.all([
        fetch('/api/latest', { cache: 'no-store' }),
        fetch('/api/history?limit=50', { cache: 'no-store' }),
      ]);
      const latestJson = await latestResponse.json();
      const historyJson = await historyResponse.json();
      setLatest(latestJson.data);
      setStorage(latestJson.storage);
      setHistory(historyJson.data || []);
      setError('');
    } catch (e) {
      setError(e.message || 'Ошибка загрузки');
    }
  }

  useEffect(() => {
    refresh();
    const timer = setInterval(refresh, 10000);
    return () => clearInterval(timer);
  }, []);

  const lastSeen = useMemo(() => latest?.receivedAt ? new Date(latest.receivedAt).toLocaleString('ru-RU') : 'Нет данных', [latest]);

  return (
    <main>
      <header>
        <div>
          <p className="eyebrow">SMART HIVE MVP</p>
          <h1>Мониторинг улья</h1>
          <p className="muted">Устройство: {latest?.deviceId || 'ожидание ESP32'} · Последняя связь: {lastSeen}</p>
        </div>
        <div className={`status ${latest ? 'online' : ''}`}>{latest ? 'Данные получены' : 'Ожидание данных'}</div>
      </header>

      {storage === 'temporary' && <div className="warning">Подключи Upstash Redis в Vercel, чтобы история сохранялась постоянно.</div>}
      {error && <div className="warning">{error}</div>}

      <section className="grid">
        <Card label="Температура" value={latest?.temperatureC} unit="°C" ok={latest?.sht31Ok !== false} />
        <Card label="Влажность" value={latest?.humidityPct} unit="%" ok={latest?.sht31Ok !== false} />
        <Card label="Спутники GPS" value={latest?.satellites} ok={latest?.gpsValid !== false} />
        <Card label="Сигнал Wi‑Fi" value={latest?.wifiRssi} unit="dBm" />
        <Card label="SHT31" value={yesNo(latest?.sht31Ok)} ok={latest?.sht31Ok !== false} />
        <Card label="microSD" value={yesNo(latest?.microSdOk)} ok={latest?.microSdOk !== false} />
        <Card label="LoRa" value={yesNo(latest?.loraOk)} ok={latest?.loraOk !== false} />
        <Card label="Прошивка" value={latest?.firmware || '—'} />
      </section>

      <section className="panel">
        <h2>Координаты</h2>
        <p>{latest?.gpsValid ? `${latest.latitude}, ${latest.longitude}` : 'GPS ещё не получил координаты. Для первого фикса вынеси антенну к окну или на улицу.'}</p>
      </section>

      <section className="panel">
        <h2>Последние измерения</h2>
        <div className="tableWrap">
          <table>
            <thead><tr><th>Время</th><th>°C</th><th>Влажность</th><th>GPS</th><th>Wi‑Fi</th></tr></thead>
            <tbody>
              {history.length ? history.map((row, index) => (
                <tr key={`${row.receivedAt}-${index}`}>
                  <td>{new Date(row.receivedAt).toLocaleTimeString('ru-RU')}</td>
                  <td>{row.temperatureC ?? '—'}</td>
                  <td>{row.humidityPct ?? '—'}%</td>
                  <td>{row.gpsValid ? 'OK' : '—'}</td>
                  <td>{row.wifiRssi ?? '—'} dBm</td>
                </tr>
              )) : <tr><td colSpan="5">Пока данных нет</td></tr>}
            </tbody>
          </table>
        </div>
      </section>
    </main>
  );
}

