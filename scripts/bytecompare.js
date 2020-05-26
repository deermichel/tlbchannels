const util = require("util");
const fs = require("fs");
const readFile = util.promisify(fs.readFile);

const projectDir = `${process.env["HOME"]}/tlbchannels`;
const evalDir = `${projectDir}/eval/out`;

// entry point
const main = async () => {
    // read bytes
    const [snd, rcv] = await Promise.all([
        readFile(`${evalDir}/json.h`),
        readFile(`${evalDir}/out.h`),
    ]);

    // count
    const sent = snd.length;
    const received = rcv.length;
    const lost = Math.max(0, sent - received);
    const inserted = Math.max(0, received - sent);
    let correct = 0;
    let corrupt = 0;
    for (let i = 0; i < Math.min(sent, received); i++) {
        if (snd[i] == rcv[i]) {
            correct++;
        } else {
            corrupt++;
        }
    }

    // stats
    const results = {
        sent, received, correct, corrupt, lost, inserted,
        byteCorrectness: correct / sent,
        byteErrorRate: 1 - (correct / sent),
    };
    console.log(results);

    return results;
}

main();
