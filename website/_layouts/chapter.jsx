export default async function EntryLayout({ children, meta, pages }) {
  const blogPages = pages
    .filter((page) => {
      return page.meta.layout === "chapter" && page.meta.lang === meta.lang;
    })
    .sort((a, b) => {
      if (a.sourcePath.endsWith("index.md")) return -1;
      if (b.sourcePath.endsWith("index.md")) return 1;
      return a.sourcePath.localeCompare(b.sourcePath);
    });

  const currentIndex = blogPages.findIndex(
    (page) => page.meta.title === meta.title,
  );
  const prev = blogPages[currentIndex - 1];
  const next = blogPages[currentIndex + 1];

  function i18n(key) {
    if (meta.lang === "en") {
      return key;
    }

    const value = {
      "Operating System in 1,000 Lines": {
        ja: "1000行で作るOS",
      },
    }[key][meta.lang];

    if (!value) {
      throw new Error(`Translation not found: ${key}`);
    }

    return value;
  }

  return (
    <html>
      <head>
        <title>{meta.title} - {i18n('Operating System in 1,000 Lines')}</title>
        <meta charset="utf-8" />
        <meta name="viewport" content="width=device-width" />
        <link rel="icon" type="image/x-icon" href="/favicon.ico" />
        <script>
          {`
            window.va = window.va || function () { (window.vaq = window.vaq || []).push(arguments); };
          `}
        </script>
        <script defer src="/_vercel/insights/script.js"></script>
      </head>
      <body className="mx-auto max-w-3xl w-full py-8 px-4">
        <header>
          <h1 className="text-center mb-8 text-xl font-bold">
            {i18n('Operating System in 1,000 Lines')} - {meta.title}
          </h1>
          <div className="mb-8 container mx-auto flex justify-center">
            <ol
              className="w-full my-0 sm:w-fit grid grid-rows-[repeat(9,auto)] grid-flow-col gap-x-4"
              start="0"
            >
              {blogPages.map((page) => {
                const active = page.meta.title === meta.title;
                return (
                  <li className="my-1">
                    <a href={page.href} className={active ? "font-bold" : ""}>
                      {page.meta.title}
                      {/* {active ? '👈' : ''} */}
                    </a>
                  </li>
                );
              })}
            </ol>
          </div>
        </header>
        <main>{children}</main>
        <footer className="mt-8 border-t border-gray-200 py-4">
          <div className="container mx-auto px-4 flex flex-col sm:flex-row justify-between items-center space-y-4 sm:space-y-0 text-lg">
            {next && <a href={next.href}>{next.meta.title} ⏩</a>}
            {prev && (
              <a href={prev.href} className="sm:-order-1">
                ⏪ {prev.meta.title}
              </a>
            )}
          </div>
        </footer>
      </body>
    </html>
  );
}
