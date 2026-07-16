const fs = require("node:fs");

const lockPath = process.argv[2];
if (!lockPath) {
  console.error("usage: node list-npm-packages.cjs <package-lock.json>");
  process.exit(2);
}

const lock = JSON.parse(fs.readFileSync(lockPath, "utf8"));
for (const [packagePath, metadata] of Object.entries(lock.packages ?? {})) {
  if (!packagePath) {
    continue;
  }
  const version = metadata?.version ?? "";
  process.stdout.write(`${packagePath}\t${version}\n`);
}
