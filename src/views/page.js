export async function buildPage(content) {
    return /*html*/`
        <p class="text-3xl font-bold underline text-red-900">
            ${content}
        </p>
        <img src="/pub/cat.png" alt="cat" />
    `;
}