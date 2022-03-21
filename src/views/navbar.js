export async function buildNavbar() {
    return /*html*/`
        <ul class="flex">
            <li class="mr-6">
                <a class="text-blue-500 hover:text-blue-800" href="/">Home</a>
            </li>
            <li class="mr-6">
                <a class="text-blue-500 hover:text-blue-800" href="/posts/13">Posts</a>
            </li>
            <li class="mr-6">
                <a class="text-blue-500 hover:text-blue-800" href="/users/someid/edit">Users</a>
            </li>
            <li class="mr-6">
                <a class="text-gray-400 cursor-not-allowed" href="/badroute">404</a>
            </li>
        </ul>
    `;
}


