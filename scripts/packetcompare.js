const util = require("util");
const fs = require("fs");
const readFile = util.promisify(fs.readFile);

// payload bytes - must match packet.h
const payloadSize = 30;

// entry point
const main = async () => {
    // read bytes
    const [snd, rcv] = await Promise.all([readFile(process.argv[2]), readFile(process.argv[3])]);

    // count
    const sent = Math.ceil(snd.length / payloadSize);
    const received = Math.ceil(rcv.length / payloadSize);
    let correct = 0; let lost = 0; let inserted = 0;
    let sndOffset = 0;
    for (let rcvIndex = 0; rcvIndex < received; rcvIndex++) {
        const rcvPacket = rcv.slice(rcvIndex * payloadSize, rcvIndex * payloadSize + payloadSize);
        
        let found = false;
        for (let sndIndex = sndOffset; sndIndex < sent; sndIndex++) {
            const sndPacket = snd.slice(sndIndex * payloadSize, sndIndex * payloadSize + payloadSize);

            if (sndPacket.compare(rcvPacket) == 0) {
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
    lost += sent - sndOffset;

    // stats
    const results = {
        sent, received, correct, lost, inserted,
        packetCorrectness: correct / sent,
        packetErrorRate: 1 - (correct / sent),
    };
    console.log(results);

    return results;
}

main();
