/** @type {import('tailwindcss').Config} */
module.exports = {
  content: [
    './pages/**/*.{js,ts,jsx,tsx,mdx}',
    './components/**/*.{js,ts,jsx,tsx,mdx}',
    './app/**/*.{js,ts,jsx,tsx,mdx}',
  ],
  theme: {
    extend: {
      typography: ({ theme }) => ({
        wider: {
          css: {
            "pre, pre[class*=language-], .rehype-code-title": {
              borderTopLeftRadius: '0 !important',
              boxShadow: '2px 2px 3px #444',
            },
            "pre, code[class*=language-], pre[class*=language-]": {
              color: '#bac1cf !important',
              backgroundColor: '#171b23 !important',
              margin: '0 0 1rem 0 !important',
            },
            ".rehype-code-title": {
              fontFamily: '"Fira Code","Fira Mono",Menlo,Consolas,"DejaVu Sans Mono",monospace',
              backgroundColor: '#474b43',
              borderTopLeftRadius: '0.2rem !important',
              borderTopRightRadius: '0.2rem',
              color: '#eaf1ff',
              width: 'fit-content',
              userSelect: 'none',
              fontSize: '0.9rem',
              paddingLeft: '1.2rem',
              marginTop: '1rem !important',
              paddingRight: '1.2rem',
              paddingTop: '0.05rem',
            }
          },
        },
        DEFAULT: {
          css: {
            'blockquote p:first-of-type::before': { content: 'none' },
            'blockquote p:first-of-type::after': { content: 'none' },
            'code::before': { content: 'none' },
            'code::after': { content: 'none' },
            a: {
              color: "rgb(76, 145, 230)",
              textDecoration: 'none',
              fontWeight: '500',
              textUnderlineOffset: "2px",
            },
            "h1": {
              marginBottom: '4rem !important',
            },
            "h1, h2, h3, h4, h5, h6, h1>a, h2>a, h3>a, h4>a, h5>a, h6>a": {
              fontWeight: 'bold !important',
            },
            "a:hover": {
              color: "rgb(66, 135, 210)",
              textDecoration: 'underline',
              fontWeight: '500',
            },
            'a.anchor': {
              color: 'var(--tw-prose-title)',
              textDecoration: 'none',
            },
            'a.anchor:hover': {
              textDecoration: 'underline !important',
              textUnderlineOffset: "4px",
            },
            "span.comment": {
              fontWeight: "bold",
              color: "#8b92a0 !important",
            },
            'blockquote': {
              fontWeight: 'normal',
              fontSize: '0.95rem',
            }
          },
        },
      }),
    },
  },
  plugins: [
    require('@tailwindcss/typography'),
  ],
}
