<template>
  <div>
    <Navbar />
    <Filtersbar />
    <NuxtPage class="min-h-screen"/>
    <Footer />
  </div>
</template>

<script setup>
    useHead({
        title: "awesome title",
        viewport: "width=device-width, initial-scale=1, maximum-scale=1",
        charset: "utf-8",
        meta: [{ name: "description", content: "My amazing site." }],
        htmlAttrs: {
            'lang': "en",
            'data-theme': "light",
        },
        bodyAttrs: {
            class: "test",
        },
        link: [
            { rel: "icon", type: "image/x-icon", href: "/assets/favicon.ico" },
        ],
    });
</script>
