import { getLanguages } from "@/lib/table-of-contents";
import { match } from "@formatjs/intl-localematcher";
import Negotiator from "negotiator";
import { headers } from "next/headers";
import { redirect } from "next/navigation";

// TODO: Replace with "en"
const DEFAULT_LANG = "ja";

export default function Home() {
  const headersList = headers();
  const acceptLanguage = headersList.get("Accept-Language");
  const preferred = new Negotiator({
    headers: { "accept-language": acceptLanguage },
  })
    .languages()
    .map((lang) => lang.split(/-_/)[0]);
  const available = getLanguages();
  const lang = match(preferred, available, DEFAULT_LANG);

  if (!available.includes(lang)) {
    // Should never happen but just in case.
    redirect(`/${DEFAULT_LANG}/welcome`);
  }

  redirect(`/${lang}/welcome`);
}
