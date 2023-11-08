<template>
    <div>
        <ol>
            <li v-for="(post, index) in data" :key="index" >
                <NuxtLink :to="`/post/${post.id}`">{{post.id}} - {{ post.title }}</NuxtLink>
            </li>
        </ol>
    </div>

</template>

<script setup>
    const db = useSupabaseClient()
    const { data, error } = await db.from('posts').select();
    // console.log(data)

</script>

