"use client";

import "../../globals.css";
import { Carter_One } from 'next/font/google'
import { FaGithub } from "react-icons/fa";
import { Content, allContents } from "contentlayer/generated";
import { usePathname } from "next/navigation";
import { useEffect, useState } from "react";
import { GoSidebarCollapse, GoSidebarExpand } from "react-icons/go";
import { BiWorld } from "react-icons/bi";
import { getLanguageNames } from "@/lib/table-of-contents";
import useMediaQuery from '@mui/material/useMediaQuery';

const carterOne = Carter_One({ subsets: ['latin'], weight: "400", style: "normal" });

export default function MdxPageLayout({
  children,
}: {
  children: React.ReactNode;
  params: { lang: string };
}) {
  // const languages = getLanguageNames();
  const pathname = usePathname();

  const [sidebarOpen, setSideBarOpen] = useState(true);
  const toggleSidebar = () => {
    setSideBarOpen(!sidebarOpen);
  };

  const sections = allContents.sort((a, b) =>
    a.filepath.localeCompare(b.filepath)
  );

  const slug2SectionNo: { [slug: string]: string } = {};
  for (const [i, { slug }] of sections.entries()) {
    slug2SectionNo[slug] = `${i + 1}.`;
  }

  let prevPage: null | Content = null;
  let nextPage: null | Content = null;
  let activeSlug: string | null;
  for (const [i, page] of sections.entries()) {
    const isActive = pathname === `/${page.slug}`;
    if (isActive) {
      activeSlug = page.slug;
      prevPage = i > 0 ? sections[i - 1] : null;
      nextPage = i < sections.length - 1 ? sections[i + 1] : null;
    }
  }


  return (
    <html>
      <body className="flex h-screen w-screen bg-white dark:bg-gray-800 dark:text-gray-200">
        <aside
          className={`w-80 h-full bg-gray-50 dark:bg-gray-700 dark:text-gray-200 shadow-md overflow-y-auto text-sm ${
            sidebarOpen ? "block" : "hidden"
          }`}
        >
          <div className="p-4">
            <h1 className={`font-bold text-gray-700 dark:text-gray-200 text-lg mb-8 text-center ${carterOne.className}`}>
              <a href="/" className="hover:text-blue-500">
                Writing an OS
                <br />
                in 1,000 Lines
              </a>
            </h1>

            {/* <div className="flex items-center justify-between">
              <BiWorld className="text-2xl mr-2" />
              <select className="bg-gray-50 border border-gray-300 text-gray-900 text-sm rounded-lg focus:ring-blue-500 focus:border-blue-500 block w-full p-2.5 dark:bg-gray-700 dark:border-gray-600 dark:placeholder-gray-400 dark:text-white ">
                {languages.map((lang) => (
                  <option key={lang.id} value={lang.id}>
                    {lang.name}
                  </option>
                ))}
              </select>
            </div> */}

            <ul className="mt-6">
              {sections.map((section) => {
                return (
                  <li key={section.slug}>
                    <a
                      href={`/${section.slug}`}
                      className={`${
                        section.slug == activeSlug
                          ? "text-blue-500 dark:text-blue-400"
                          : "text-gray-600 dark:text-gray-200"
                      } hover:text-blue-400 block px-4 py-2`}
                    >
                      <span className="text-gray-400 mr-2">
                        {slug2SectionNo[section.slug]}
                      </span>
                      {section.title}
                    </a>
                  </li>
                );
              })}
            </ul>
          </div>

          <a
            className="bg-gray-200 dark:bg-gray-800 hover:bg-gray-300 dark:hover:bg-gray-900 text-gray-800 dark:text-gray-300 block rounded-lg mx-6 mb-4 text-center px-8 py-2 no-underline"
            href="https://github.com/nuta/microkernel-from-scratch"
            target="_blank"
          >
            <FaGithub className="inline-block" />
            &nbsp; GitHubを開く
          </a>
        </aside>

        <main className="w-full overflow-y-scroll">
          <button
            onClick={toggleSidebar}
            className={`flex items-center justify-center h-12 w-12 focus:outline-none text-gray-800 bg-white dark:bg-gray-200 dark:text-gray-900 border-2 rounded-lg sticky top-4 left-4`}
          >
            {sidebarOpen ? <GoSidebarExpand /> : <GoSidebarCollapse />}
          </button>
          <div className="flex-grow prose lg:prose-wider dark:prose-invert prose-neutral mx-auto max-w-4xl w-full px-4">
            {children}
            <div className="mt-20 mb-16 flex justify-between">
              {(prevPage && (
                <a
                  href={`/${prevPage.slug}`}
                  className="bg-gray-200 dark:bg-gray-700 hover:bg-gray-300 dark:hover:bg-gray-600 text-gray-800 dark:text-gray-300 font-bold py-2 px-4 rounded no-underline"
                >
                  &lt; {slug2SectionNo[prevPage.slug]} {prevPage.title}
                </a>
              )) || <div></div>}
              {nextPage && (
                <a
                  href={`/${nextPage.slug}`}
                  className="bg-gray-200 dark:bg-gray-700 hover:bg-gray-300 dark:hover:bg-gray-600 text-gray-800 dark:text-gray-300 font-bold py-2 px-4 rounded no-underline"
                >
                  {slug2SectionNo[nextPage.slug]} {nextPage.title} &gt;
                </a>
              )}
            </div>
          </div>
        </main>
      </body>
    </html>
  );
}
