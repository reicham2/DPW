<template>
  <div>
    <nav class="global-nav">
      <div class="global-nav-inner">
        <router-link to="/" class="global-nav-logo">
          <img src="/logo.svg" alt="DPWeb" class="global-nav-logo-img" />
        </router-link>
      </div>
    </nav>
    <div class="app">
      <router-view />
    </div>
  </div>
</template>
