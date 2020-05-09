const util = require("util");
const fs = require("fs");
const readFile = util.promisify(fs.readFile);

const projectDir = `${process.env["HOME"]}/tlbchannels`;
const evalDir = `${projectDir}/eval/out`;

// parse csv logfile
const parseLogfile = (csv) => {
    const rows = csv.split("\n").slice(0, -1).map((row) => row.split(","));
    const packets = rows.map(([start, end, data]) => ({ 
        start, end, data,
    }));
    return packets;
};

// entry point
const main = async () => {
    // read bytes
    const [sndLog, rcvLog] = await Promise.all([
        readFile(`${evalDir}/snd_packets_log.csv`, "utf8"),
        readFile(`${evalDir}/rcv_packets_log.csv`, "utf8"),
    ]);
    const sndData = parseLogfile(sndLog).map((packet) => packet.data.substring(3)); // payload offset
    const rcvData = parseLogfile(rcvLog).map((packet) => packet.data.substring(3)); // payload offset
    const sndBytes = sndData.join("").split(" ").slice(0, -1);
    const rcvBytes = rcvData.join("").split(" ").slice(0, -1);

    // compare bytes
    let correct = 0;
    let lost = 0;
    let inserted = 0;
    let sndOffset = 0;
    for (let rcvIndex = 0; rcvIndex < rcvBytes.length; rcvIndex++) {
        const rcv = rcvBytes[rcvIndex];

        let found = false;
        
        for (let sndIndex = sndOffset; sndIndex < sndBytes.length; sndIndex++) {
            const snd = sndBytes[sndIndex];

            if (snd == rcv) {
                lost += sndIndex - sndOffset;
                sndOffset = sndIndex + 1;
                correct++;
                found = true;
                break;
            }
        }

        if (!found) {
            inserted++;
        }
    }
    lost += sndBytes.length - sndOffset;

    // stats
    const results = {
        sent: sndBytes.length,
        received: rcvBytes.length,
        correct, lost, inserted,
        byteCorrectness: correct / sndBytes.length,
        byteErrorRate: 1 - (correct / sndBytes.length),
    };
    console.log(results);

    return results;
}

main();
