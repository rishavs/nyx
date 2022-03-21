import { stitchHTML } from "../../src/views/stitcher";

export async function onRequest(context) {

    // page = new Page();
    // html = new Html (Header, Page, )

    const content = `SHOW USER ${context.params.id}`
    const html = await stitchHTML(content);

    return new Response(html, {
        headers: {
            'content-type': 'text/html;charset=UTF-8',
        },
    });
}