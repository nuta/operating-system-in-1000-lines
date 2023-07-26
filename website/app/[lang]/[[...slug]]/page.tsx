import Image from "next/image";
import { useMDXComponent } from "next-contentlayer/hooks";
import { allContents } from "contentlayer/generated";
import type { Metadata } from "next";
import Sv32 from "@/components/Sv32";
import Link from "next/link";
import { getContentBySlug } from "@/lib/table-of-contents";
import { notFound } from "next/navigation";

const Info = ({ children }) => (
  <div className="bg-gray-100 dark:bg-gray-700 border-l-8 border-l-blue-500 p-4 rounded-md my-4">
    {children}
  </div>
);

const Warning = ({ children }) => (
  <div className="bg-gray-100 dark:bg-gray-700 border-l-8 border-l-yellow-500 p-4 rounded-md my-4">
    {children}
  </div>
);

const EnagaBook = ({ chapter, section, title }) => (
  <div className="inline-block mx-2">
    <a
      href="https://www.shuwasystem.co.jp/book/9784798068718.html"
      className="hover:no-underline font-normal"
      target="_blank"
    >
      <span
        className="rounded-l-md text-gray-900 px-1 border border-gray-400"
        style={{ backgroundColor: "rgb(163, 217, 250)" }}
      >
        エナガ本
      </span>
      <span className="rounded-r-md dark:bg-gray-300 bg-gray-200 text-gray-900 px-2 border-t border-r border-b border-gray-400">
        {chapter && `${chapter}章`}
        {section && `${section}節`}
        {title && ` (${title})`}
      </span>
    </a>
  </div>
);

const MDXComponents = {
  Image,
  Link,
  Info,
  Warning,
  Sv32,
  EnagaBook,
};

export async function generateStaticParams() {
  return allContents
    .map((content) => content.slugAsArray)
    .map((slug) => ({ slug }));
}

function slugFromParams(params: { lang: string; slug: string[] | string }) {
  return [
    params.lang,
    ...(Array.isArray(params.slug) ? params.slug : [params.slug]),
  ].join("/");
}

export async function generateMetadata({ params }): Promise<Metadata> {
  const slug = slugFromParams(params);
  let content;
  try {
    content = getContentBySlug(slug);
  } catch (e) {
    if (e instanceof Error && e.message.includes("not found")) {
      notFound();
    }
  }

  return {
    metadataBase: new URL("https://operating-system-in-1000-lines.vercel.app"),
    title: `${content.title} - Writing an OS in 1,000 Lines`,
    icons: {
      icon: "/favicon.ico",
    },
    openGraph: {
      title: content.title,
      siteName: "operating-system-in-1000-lines.vercel.app",
      type: "website",
      url: `https://operating-system-in-1000-lines.vercel.app/${content.slug}`,
      images: [
        {
          url: `https://operating-system-in-1000-lines.vercel.app/api/og`,
          width: 1200,
          height: 600,
        },
      ],
    },
    twitter: {
      title: content.title,
      description: content.title,
      card: "summary_large_image",
      creator: "@seiyanuta",
      images: [
        `https://operating-system-in-1000-lines.vercel.app/api/og`,
      ],
    },
  };
}

export default function Page({
  params,
}: {
  params: { lang: string; slug: string[] };
}) {
  const slug = slugFromParams(params);
  let content;
  try {
    content = getContentBySlug(slug);
  } catch (e) {
    if (e instanceof Error && e.message.includes("not found")) {
      notFound();
    }
  }

  const MDXContent = useMDXComponent(content.body.code);

  return (
    <>
      <div className="mt-8 mb-6 text-center ">
        <h1 className="mb-1 text-3xl font-bold dark:text-slate-200">
          {content.title}
        </h1>
      </div>
      <div>
        {/* @ts-ignore RSC may return Promise<Element> */}
        <MDXContent components={MDXComponents} />
      </div>
    </>
  );
}
