import './styles.css';

export const metadata = {
  title: 'SmartHive Monitor',
  description: 'SmartHive temperature, humidity, sound, passage and location telemetry',
};

export default function RootLayout({ children }) {
  return (
    <html lang="ru">
      <body>{children}</body>
    </html>
  );
}
