import './styles.css';

export const metadata = {
  title: 'SmartHive Monitor',
  description: 'Live telemetry dashboard for the SmartHive MVP',
};

export default function RootLayout({ children }) {
  return (
    <html lang="ru">
      <body>{children}</body>
    </html>
  );
}

