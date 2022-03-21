import { stitchHTML } from "../src/views/stitcher";
export async function onRequest(context) {

    // page = new Page();
    // html = new Html (Header, Page, )

    const url = new URL(context.request.url);
    if (url.pathname.startsWith('/pub/')) {
        return await context.next();
    }
    const content = url.pathname === '/' ? "HOME":"404 Error"
    const html = await stitchHTML(content);

    return new Response(html, {
        headers: {
            'content-type': 'text/html;charset=UTF-8',
        },
    });
}