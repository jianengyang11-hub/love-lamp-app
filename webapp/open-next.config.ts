import cache from "@opennextjs/cloudflare/kvCache";

export default {
  default: {
    override: {
      wrapper: "cloudflare-node",
      converter: "edge",
      incrementalCache: async () => cache,
    },
  },
};