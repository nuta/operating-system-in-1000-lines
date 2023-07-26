import { defineDocumentType, makeSource } from "contentlayer/source-files";
import remarkGfm from "remark-gfm";
import rehypeSlug from "rehype-slug";
import rehypeAutolinkHeadings from "rehype-autolink-headings";
import rehypeCodeTitles from "rehype-code-titles";
import rehypePrism from "rehype-prism-plus";

export const Content = defineDocumentType(() => ({
  name: "Content",
  filePathPattern: `**/*.mdx`,
  contentType: "mdx",
  fields: {
    title: {
      type: "string",
      required: true,
    },
  },
  computedFields: {
    filepath: {
      type: "string",
      resolve: (doc) => doc._raw.sourceFilePath,
    },
    slug: {
      type: "string",
      // ja/01-introduction/03-assembly.mdx => 'ja/introduction/assembly'
      resolve: (doc) => {
        const slug = doc._raw.flattenedPath.replace(/\.mdx$/, "");
        return slug.split('/').map((s) => s.replace(/^\d+\-/, '')).join('/');
      }
    },
    slugAsArray: {
      type: 'list',
      resolve: (doc) => {
        // ja/01-introduction/03-assembly.mdx => ['ja', 'introduction', 'assembly']
        const slug = doc._raw.flattenedPath.replace(/\.mdx$/, "");
        return slug.split('/').map((s) => s.replace(/^\d+\-/, ''));
      }
    },
  },
}));

export default makeSource({
  contentDirPath: "contents",
  documentTypes: [Content],
  mdx: {
    remarkPlugins: [remarkGfm],
    rehypePlugins: [
      rehypeSlug,
      rehypeCodeTitles,
      rehypePrism,
      [
        rehypeAutolinkHeadings,
        {
          behavior: "wrap",
          properties: {
            className: ["anchor"],
          },
        },
      ],
    ],
  },
});
