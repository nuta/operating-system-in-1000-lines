export default async function IndexLayout({ children, meta, pages }) {
    return (
      <html>
        <head>
          <title>Operating System in 1,000 Lines</title>
          <meta charset="utf-8" />
          <meta name="viewport" content="width=device-width" />
        </head>
        <body className="mx-auto max-w-3xl w-full py-8 px-4">
          <header>
            <h1 className="text-center mb-4 text-xl font-bold">
              Operating System in 1,000 Lines
            </h1>
          </header>
          <main className="container mx-auto flex justify-center">
            <ul>
                <li><a href="/ja" className="text-lg">Japanese (日本語)</a></li>
            </ul>
          </main>
        </body>
      </html>
    );
}