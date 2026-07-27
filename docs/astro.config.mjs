import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

const docsBase = '/gode';
const withBase = (path) => (docsBase === '/' ? path : `${docsBase}${path}`);

export default defineConfig({
  site: 'https://godothub.github.io',
  base: docsBase,
  integrations: [
    starlight({
      title: {
        en: 'Gode',
        'zh-CN': 'Gode',
      },
      description:
        'Production documentation for Gode, the TypeScript scripting runtime for Godot.',
      favicon: 'https://legacy.godothub.com/icon/godothub.png',
      defaultLocale: 'root',
      locales: {
        root: {
          label: 'English',
          lang: 'en',
        },
        zh: {
          label: '简体中文',
          lang: 'zh-CN',
        },
      },
      social: [
        {
          icon: 'github',
          label: 'GitHub',
          href: 'https://github.com/godothub/gode',
        },
      ],
      editLink: {
        baseUrl: 'https://github.com/godothub/gode/edit/main/docs/',
      },
      components: {
        SiteTitle: './src/components/SiteTitle.astro',
        SocialIcons: './src/components/HeaderLinks.astro',
      },
      customCss: ['./src/styles/gode.css'],
      lastUpdated: true,
      tableOfContents: {
        minHeadingLevel: 2,
        maxHeadingLevel: 4,
      },
      sidebar: [
        {
          label: 'Start',
          translations: { 'zh-CN': '开始' },
          items: [
            'index',
            'getting-started/installation',
            'getting-started/first-script',
            'getting-started/typescript-config',
          ],
        },
        {
          label: 'Core Guides',
          translations: { 'zh-CN': '核心指南' },
          items: [
            'guides/godot-api',
            'guides/npm-packages',
            'guides/interoperability',
            'guides/metadata',
            'guides/exporting',
            'guides/debugging',
          ],
        },
        {
          label: 'Reference',
          translations: { 'zh-CN': '参考' },
          items: [
            'reference/project-structure',
            'reference/build-from-source',
          ],
        },
      ],
      head: [
        {
          tag: 'meta',
          attrs: {
            name: 'theme-color',
            content: '#07110d',
          },
        },
        {
          tag: 'script',
          attrs: {
            src: withBase('/set-default-theme.js'),
          },
        },
      ],
    }),
  ],
});
