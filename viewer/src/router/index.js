import { createRouter, createWebHistory, createWebHashHistory } from "vue-router";
import DataViewer from "../views/DataViewer.vue";
import DeviceConfig from "../views/DeviceConfig.vue";
import Multi from "../views/Multi.vue";

const routes = [
  { path: "/", name: "viewer", component: DataViewer },
  { path: "/config", name: "config", component: DeviceConfig },
  { path: "/multi2", name: "Multi", component: Multi,},
];

const single = import.meta.env.MODE === "single";

const baseUrl = import.meta.env.PROD ? import.meta.env.BASE_URL : "";
const router = createRouter({
  history: single ? createWebHashHistory(baseUrl) : createWebHistory(baseUrl),
  routes,
});

export default router;
