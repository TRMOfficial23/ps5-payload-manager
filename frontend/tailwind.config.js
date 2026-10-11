/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        'ps-blue': '#59e7dc',
        'ps-blue-glow': 'rgba(89, 231, 220, 0.18)',
        'ps-black': '#080b12',
        'ps-surface': '#101621',
        'ps-card': '#121b29',
        'ps-border': '#243043',
      },
      borderRadius: {
        'ps-xl': '1rem',
        'ps-2xl': '1.5rem',
        'ps-3xl': '2rem',
      },
      fontFamily: {
        'ps5': ["Inter", "system-ui", "sans-serif"],
      }
    },
  },
  plugins: [],
}
