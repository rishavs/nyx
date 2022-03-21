import { buildHeaders } from ".//headers";
import { buildNavbar } from "./navbar";
import { buildPage } from "./page";

export async function stitchHTML(content) {
    const htmlHeaders   = await buildHeaders();
    const htmlNavbar    = await buildNavbar();

    const htmlPage      = await buildPage(content);

    return /*html*/`
        <!doctype html>
        <html>
            ${htmlHeaders}
            <body>
                ${htmlNavbar}
                ${htmlPage}
            </body>
        </html>
    `;
}