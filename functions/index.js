export async function onRequest(context) {
    // Contents of context object
    const {
        request, // same as existing Worker API
        env, // same as existing Worker API
        params, // if filename includes [id] or [[path]]
        waitUntil, // same as ctx.waitUntil in existing Worker API
        next, // used for middleware or to fetch assets
        data, // arbitrary space for passing data between middlewares
    } = context;

    // page = new Page();
    // html = new Html (Header, Page, )

    const assets = await next();
    console.log (assets)
// console.log(response);

    const html = /*html*/`
        <!doctype html>
        <html>
            <head>
                <meta charset="UTF-8">
                <meta name="viewport" content="width=device-width, initial-scale=1.0">
                <link href="pub/style.css" rel="stylesheet">
            </head>
            <body>
            <h1 class="text-3xl font-bold underline text-red-900">
                HOME INDEX
                ${assets}
            </h1>
            </body>
        </html>
    `;

    return new Response(html, {
        headers: {
            'content-type': 'text/html;charset=UTF-8',
        },
    });
}
