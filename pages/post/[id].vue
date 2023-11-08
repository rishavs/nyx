<template>
    <div>
        <p class="text-red-700">This is Post {{ $route.params.id }}</p>
        <h1>{{data[0].id}} : {{data[0].title}}</h1>
        <p>{{data[0].description}}</p>
        <NuxtPage/>
    </div>
</template>

<script setup>
    const route = useRoute()
    const db = useSupabaseClient()

    const { data, error } = await useLazyAsyncData('posts', async () => {
        const { data } = await db.from("posts").select().match({id: route.params.id})
        return data
    })

</script>