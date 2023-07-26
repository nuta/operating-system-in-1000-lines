import { Content, allContents } from "contentlayer/generated";

export interface Toc {
  langName: string;
  chapters: {
    index: string;
    title: string;
    sectionSlugs: string[];
  }[];
}

const toc: { [lang: string]: Toc } = {
  ja: {
    langName: "日本語",
    chapters: [
      {
        index: "1",
        title: "はじめに",
        sectionSlugs: [
          "ja/introduction/welcome",
          "ja/introduction/setting-up-development-environment",
          "ja/introduction/overview",
          "ja/introduction/assembly",
        ],
      },
      {
        index: "2",
        title: "基本機能",
        sectionSlugs: [
          "ja/kernel/boot",
          "ja/kernel/hello-world",
          "ja/kernel/libc",
          "ja/kernel/kernel-panic",
          "ja/kernel/exception",
          "ja/kernel/memory-allocation",
          "ja/kernel/process",
          "ja/kernel/page-table",
          "ja/kernel/first-application",
          "ja/kernel/user-mode",
          "ja/kernel/system-call",
        ],
      },
      {
        index: "3",
        title: "発展的機能",
        sectionSlugs: [
          "ja/advanced-features/virtio-blk",
          "ja/advanced-features/file-system",
        ],
      },
      {
        index: "4",
        title: "おわりに",
        sectionSlugs: ["ja/conclusion/conclusion"],
      },
      {
        index: "A",
        title: "付録",
        sectionSlugs: ["ja/appendix/debugging-paging"],
      },
    ],
  },
};

export function getContentBySlug(slug: string): Content {
  const content = allContents.find((content) => content.slug === slug);
  if (!content) {
    throw new Error(`Page with slug "${slug}" not found`);
  }

  return content;
}

export function loadTableOfContents(lang: string): Toc {
  return toc[lang] ?? toc["ja"];
}

export function getLanguages(): string[] {
  return Object.keys(toc);
}

export function getLanguageNames(): { id: string; name: string }[] {
  return Object.entries(toc).map(([id, toc]) => ({
    id,
    name: toc.langName,
  }));
}
