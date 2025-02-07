import { defineConfig } from 'vitepress'
import {
  groupIconMdPlugin,
  groupIconVitePlugin,
} from 'vitepress-plugin-group-icons'


// https://vitepress.dev/reference/site-config
export default defineConfig({
  title: "OS in 1,000 Lines",
  description: "Write your first operating system from scratch, in 1K LoC.",
  cleanUrls: true,
  markdown: {
    config(md) {
      md.use(groupIconMdPlugin)
    },
  },
  vite: {
    plugins: [
      groupIconVitePlugin()
    ],
  },
  locales: {
    ko: {
      label: '한국어',
      lang: 'ko',
      themeConfig: {
        sidebar: [
          {
            text: '목차',
            items: [
              { link: '/ko/', text: '00. 들어가며' },
              { link: '/ko/01-setting-up-development-environment', text: '01. 환경설정' },
              { link: '/ko/02-assembly', text: '02. RISC-V 입문' },
              { link: '/ko/03-overview', text: '03. 기능 및 구조 미리보기' },
              { link: '/ko/04-boot', text: '04. 부트' },
              { link: '/ko/05-hello-world', text: '05. Hello World!' },
              { link: '/ko/06-libc', text: '06. C 표준 라이브러리' },
              { link: '/ko/07-kernel-panic', text: '07. 커널 패닉' },
              { link: '/ko/08-exception', text: '08. 예외' },
              { link: '/ko/09-memory-allocation', text: '09. 메모리 할당' },
              { link: '/ko/10-process', text: '10. 프로세스' },
              { link: '/ko/11-page-table', text: '11. 페이지 테이블' },
              { link: '/ko/12-application', text: '12. 애플리케이션' },
              { link: '/ko/13-user-mode', text: '13. 유저 모드' },
              { link: '/ko/14-system-call', text: '14. 시스템 콜' },
              { link: '/ko/15-virtio-blk', text: '15. 디스크 I/O' },
              { link: '/ko/16-file-system', text: '16. 파일 시스템' },
              { link: '/ko/17-outro', text: '17. 끝내며' },
            ]
          },
          {
            text: 'Links',
            items: [
              { link: 'https://github.com/nuta/operating-system-in-1000-lines', text: 'GitHub repository' },
              { link: 'https://www.hanmoto.com/bd/isbn/9784798068718', text: 'Design and Implementation of Microkernels' },
              { link: '/ja', text: '日本語版' },
              { link: '/en', text: 'English version' },
            ]
          },
        ],
        socialLinks: [
          { icon: 'github', link: 'https://github.com/nuta/operating-system-in-1000-lines' }
        ]
      }
    },
    en: {
      label: 'English',
      lang: 'en',
      themeConfig: {
        sidebar: [
          {
            text: 'Table of Contents',
            items: [
              { link: '/en/', text: '00. Intro' },
              { link: '/en/01-setting-up-development-environment', text: '01. Getting Started' },
              { link: '/en/02-assembly', text: '02. RISC-V 101' },
              { link: '/en/03-overview', text: '03. Overview' },
              { link: '/en/04-boot', text: '04. Boot' },
              { link: '/en/05-hello-world', text: '05. Hello World!' },
              { link: '/en/06-libc', text: '06. C Standard Library' },
              { link: '/en/07-kernel-panic', text: '07. Kernel Panic' },
              { link: '/en/08-exception', text: '08. Exception' },
              { link: '/en/09-memory-allocation', text: '09. Memory Allocation' },
              { link: '/en/10-process', text: '10. Process' },
              { link: '/en/11-page-table', text: '11. Page Table' },
              { link: '/en/12-application', text: '12. Application' },
              { link: '/en/13-user-mode', text: '13. User Mode' },
              { link: '/en/14-system-call', text: '14. System Call' },
              { link: '/en/15-virtio-blk', text: '15. Disk I/O' },
              { link: '/en/16-file-system', text: '16. File System' },
              { link: '/en/17-outro', text: '17. Outro' },
            ]
          },
          {
            text: 'Links',
            items: [
              { link: 'https://github.com/nuta/operating-system-in-1000-lines', text: 'GitHub repository' },
              { link: 'https://www.hanmoto.com/bd/isbn/9784798068718', text: 'Design and Implementation of Microkernels' },
              { link: '/ja', text: '日本語版' },
              { link: '/zh', text: '简体中文版' },
              { link: '/ko', text: '한국어판' },
            ]
          },
        ],
        socialLinks: [
          { icon: 'github', link: 'https://github.com/nuta/operating-system-in-1000-lines' }
        ]
      }
    },
    ja: {
      label: '日本語',
      lang: 'ja',
      themeConfig: {
        sidebar: [
          {
            text: '目次',
            items: [
              { link: '/ja/', text: '00. はじめに' },
              { link: '/ja/01-setting-up-development-environment', text: '01. 開発環境' },
              { link: '/ja/02-assembly', text: '02. RISC-V入門' },
              { link: '/ja/03-overview', text: '03. OSの全体像' },
              { link: '/ja/04-boot', text: '04. ブート' },
              { link: '/ja/05-hello-world', text: '05. Hello World!' },
              { link: '/ja/06-libc', text: '06. C標準ライブラリ' },
              { link: '/ja/07-kernel-panic', text: '07. カーネルパニック' },
              { link: '/ja/08-exception', text: '08. 例外処理' },
              { link: '/ja/09-memory-allocation', text: '09. メモリ割り当て' },
              { link: '/ja/10-process', text: '10. プロセス' },
              { link: '/ja/11-page-table', text: '11. ページテーブル' },
              { link: '/ja/12-application', text: '12. アプリケーション' },
              { link: '/ja/13-user-mode', text: '13. ユーザーモード' },
              { link: '/ja/14-system-call', text: '14. システムコール' },
              { link: '/ja/15-virtio-blk', text: '15. ディスク読み書き' },
              { link: '/ja/16-file-system', text: '16. ファイルシステム' },
              { link: '/ja/17-outro', text: '17. おわりに' },
            ]
          },
          {
            text: 'リンク',
            items: [
              { link: '/en', text: 'English version' },
              { link: '/zh', text: '简体中文版' },
              { link: 'https://github.com/nuta/operating-system-in-1000-lines', text: 'GitHubリポジトリ' },
              { link: 'https://www.hanmoto.com/bd/isbn/9784798068718', text: 'マイクロカーネル本' },
              { link: '/ko', text: '한국어판' },
            ]
          },
        ],
        socialLinks: [
          { icon: 'github', link: 'https://github.com/nuta/operating-system-in-1000-lines' }
        ]
      }
    },
    zh: {
      label: '简体中文',
      lang: 'zh',
      themeConfig: {
        sidebar: [
          {
            text: '目录',
            items: [
              { link: '/zh/', text: '00. 简介' },
              { link: '/zh/01-setting-up-development-environment', text: '01. 入门' },
              { link: '/zh/02-assembly', text: '02. RISC-V 101' },
              { link: '/zh/03-overview', text: '03. 总览' },
              { link: '/zh/04-boot', text: '04. 引导' },
              { link: '/zh/05-hello-world', text: '05. Hello World!' },
              { link: '/zh/06-libc', text: '06. C 标准库' },
              { link: '/zh/07-kernel-panic', text: '07. 内核恐慌（Kernel Panic）' },
              { link: '/zh/08-exception', text: '08. 异常' },
              { link: '/zh/09-memory-allocation', text: '09. 内存分配' },
              { link: '/zh/10-process', text: '10. 进程' },
              { link: '/zh/11-page-table', text: '11. 页表' },
              { link: '/zh/12-application', text: '12. 应用程序' },
              { link: '/zh/13-user-mode', text: '13. 用户模式' },
              { link: '/zh/14-system-call', text: '14. 系统调用' },
              { link: '/zh/15-virtio-blk', text: '15. 磁盘 I/O' },
              { link: '/zh/16-file-system', text: '16. 文件系统' },
              { link: '/zh/17-outro', text: '17. 结语' },
            ]
          },
          {
            text: 'Links',
            items: [
              { link: 'https://github.com/nuta/operating-system-in-1000-lines', text: 'GitHub repository' },
              { link: '/en', text: 'English version' },
              { link: '/ja', text: '日本語版' },
            ]
          },
        ],
        socialLinks: [
          { icon: 'github', link: 'https://github.com/nuta/operating-system-in-1000-lines' }
        ]
      }
    }
  },
})
