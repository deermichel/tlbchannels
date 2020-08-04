const util = require("util");
const fs = require("fs");
const glob = require("glob");
const { parse } = require("path");
const { writeFileSync, mkdirSync, existsSync, readFileSync } = fs;
const readFile = util.promisify(fs.readFile);

const parseFile = (file) => {
    const content = readFileSync(file, "utf8");
    const config = file.match(/..eval\/(.*)\//)[1];
    const correctPackets = content.match(/correct: (.*),/)[1];
    const timeReceiver = content.match(/time: (.*) s/)[1];
    const bandwidth = (correctPackets * 30) / timeReceiver / 1000;
    const packetError = content.match(/packetErrorRate: (.*)/)[1];
    const byteError = content.match(/byteErrorRate: (.*)/)[1];
    const parsed = `${config},${bandwidth},${byteError},${packetError}`
    console.log(parsed);
    return parsed;
}

// entry point
const main = () => {
    let result = "scen,evict,file,sndwindow,rs,iter,bandwidth,byteerror,packeterror\n";
    glob(process.argv[2] + "/**/result.txt", (err, files) => {
        files.forEach((f) => {
            if (existsSync(f.replace("result.txt", "finish.txt"))) {
                result += `${parseFile(f)}\n`;
            }
        });
        writeFileSync("result.csv", result);
    });
};

main();
