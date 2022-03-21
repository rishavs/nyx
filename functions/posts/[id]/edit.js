import { stitchHTML } from "../../../src/views/stitcher";

export async function onRequest(context) {

    // page = new Page();
    // html = new Html (Header, Page, )
    const url = new URL(context.request.url);
    console.log(url.pathname)
    if (url.pathname.startsWith('/pub/')) {
        return await context.next();
    }
    const content = `POSTS EDIT ${context.params.id}`
    const html = await stitchHTML(content);

    return new Response(html, {
        headers: {
            'content-type': 'text/html;charset=UTF-8',
        },
    });
}