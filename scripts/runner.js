const node_ssh = require("node-ssh");
const util = require("util");
const fs = require("fs");
const readFile = util.promisify(fs.readFile);
const { writeFileSync, mkdirSync } = fs;
const exec = util.promisify(require("child_process").exec);

const verbose = process.argv.includes("-v");
const vm1address = "192.168.122.190";
const vm2address = "192.168.122.201";
const receiverTimeout = 40000; // ms
const payloadSize = 30; // bytes

const projectDir = `${process.env["HOME"]}/tlbchannels`;
const senderDir = `${projectDir}/src/sender`;
const receiverDir = `${projectDir}/src/receiver`;
const evalDir = `${projectDir}/eval`;
const remoteDir = "/home/user";

let vm1ssh, vm2ssh;

// parse csv logfile
const parseLogfile = (csv) => {
    const rows = csv.split("\n").slice(0, -1).map((row) => row.split(","));
    const packets = rows.map(([start, end, data]) => ({ 
        start, end, data,
        // data: data.split(" ").slice(0, -1),
    }));
    return packets;
};

// compile locally and copy binaries
const compileAndCopy = async () => {
    // compile
    const [m1, m2] = await Promise.all([
        exec("make", { cwd: senderDir }),
        exec("make", { cwd: receiverDir }),
    ]);
    if (verbose) {
        console.log(m1.stdout);
        console.log(m2.stdout);
    }
    console.log("compiled");

    // copy binaries
    await Promise.all([
        vm1ssh.putFile(`${receiverDir}/receiver`, `${remoteDir}/receiver`),
        vm2ssh.putFile(`${senderDir}/sender`, `${remoteDir}/sender`),
    ]);
    console.log("binaries copied");
};

// connect to vms
const connect = async () => {
    vm1ssh = new node_ssh();
    vm2ssh = new node_ssh();
    await Promise.all([
        vm1ssh.connect({
            host: vm1address,
            username: "user",
            privateKey: `${process.env["HOME"]}/.ssh/id_rsa`
        }),
        vm2ssh.connect({
            host: vm2address,
            username: "user",
            privateKey: `${process.env["HOME"]}/.ssh/id_rsa`
        }),
    ]);
    console.log("ssh connected");
};

// disconnect ssh
const disconnect = async () => {
    vm1ssh.dispose();
    vm2ssh.dispose();
    console.log("ssh disconnected");
}

// run config and evaluate results
const run = async (sndFile, rcvFile, sndArgs, rcvArgs, destDir) => {
    console.log(`[sndFile: ${sndFile} | rcvFile: ${rcvFile} | sndArgs: ${sndArgs} | rcvArgs: ${rcvArgs}]`);
    mkdirSync(destDir, { recursive: true });

    // execute binaries
    const killReceiver = setTimeout(() => vm1ssh.execCommand("pkill receiver"), receiverTimeout);
    let transferDuration = "timeout";
    let sendDuration = "timeout";
    try {
        const [r1, r2] = await Promise.all([
            vm1ssh.exec("./receiver", ["-o", rcvFile, rcvArgs], { cwd: remoteDir }),
            vm2ssh.exec("./sender", ["-f", sndFile, sndArgs], { cwd: remoteDir }),
        ]);
        transferDuration = parseFloat(r1.match(/time: (.*) s/)[1]);
        sendDuration = parseFloat(r2.match(/time: (.*) s/)[1]);
        if (verbose) {
            console.log("--- receiver stdout ---");
            console.log(r1);
            console.log("--- sender stdout ---");
            console.log(r2);
            console.log("---");
        }
    } catch (error) {
        if (error.message === "Terminated") {
            console.log("error: receiver timed out");
        } else {
            console.log(error);
        }
        
    }
    clearInterval(killReceiver);
    console.log("binaries executed");

    // retrieve artifacts
    await Promise.all([
        vm1ssh.getFile(`${destDir}/rcv_packets_log.csv`, `${remoteDir}/packets_log.csv`),
        vm1ssh.getFile(`${destDir}/${rcvFile}`, `${remoteDir}/${rcvFile}`),
        vm2ssh.getFile(`${destDir}/snd_packets_log.csv`, `${remoteDir}/packets_log.csv`),
        vm2ssh.getFile(`${destDir}/${sndFile}`, `${remoteDir}/${sndFile}`),
    ]);
    console.log("retrieved artifacts");

    // read logs
    const [sndLog, rcvLog] = await Promise.all([
        readFile(`${destDir}/snd_packets_log.csv`, "utf8"),
        readFile(`${destDir}/rcv_packets_log.csv`, "utf8"),
    ]);
    const sndPackets = parseLogfile(sndLog).map((packet) => packet.data);
    const rcvPackets = parseLogfile(rcvLog).map((packet) => packet.data);

    // compare data
    let correctPackets = 0;
    let lostPackets = 0;
    let insertedPackets = 0;
    let sndOffset = 0;
    for (let rcvIndex = 0; rcvIndex < rcvPackets.length; rcvIndex++) {
        // console.log(rcvIndex, rcvPackets.length);
        const rcv = rcvPackets[rcvIndex];

        let found = false;

        for (let sndIndex = sndOffset; sndIndex < sndPackets.length; sndIndex++) {
            const snd = sndPackets[sndIndex];

            if (snd === rcv) {
                lostPackets += sndIndex - sndOffset;
                sndOffset = sndIndex + 1;
                correctPackets++;
                found = true;
                break;
            }
        }

        if (!found) {
            insertedPackets++;
        }
    }
    lostPackets += sndPackets.length - sndOffset;

    // results
    const results = {
        sndFile, rcvFile, sndArgs, rcvArgs,
        lostPackets, insertedPackets, transferDuration, sendDuration, correctPackets,
        sentPackets: sndPackets.length,
        receivedPackets: rcvPackets.length,
        sentBytes: sndPackets.length * payloadSize,
        receivedBytes: rcvPackets.length * payloadSize,
        bandwidth: ((correctPackets * payloadSize) / transferDuration) / 1000, // kB/s
        maxBandwidth: ((sndPackets.length * payloadSize) / sendDuration) / 1000, // kB/s
        rawBandwidth: ((rcvPackets.length * payloadSize) / transferDuration) / 1000, // kB/s
        errorRate: 1 - (correctPackets / sndPackets.length),
    };
    writeFileSync(`${destDir}/results.json`, JSON.stringify(results, null, 4));
    if (verbose && transferDuration !== "timeout") console.log("results:", results);
    console.log("results saved");

    return results;
};

// entry point
const main = async () => {
    await connect();
    await compileAndCopy();

    let results = [];
    // for (let w = 10; w < 50; w += 10) {
    //     for (let r = 0; r < 2; r++) {
    //         results.push(await run("text.txt", "out.txt", w, `${evalDir}/w${w}_r${r}`));
    //     }
    // }
    // for (let sw = 28; sw <= 31; sw++) {
    //     for (let rw = 8; rw <= 9; rw++) {
    //         for (let t = 3; t <= 5; t++) {
    //             try {
    //                 results.push(await run("text.txt", "out.txt", sw, rw, t, `${evalDir}/sw${sw}_rw${rw}_t${t}`));
    //             } catch (e) {
    //                 console.log(e);
    //             }
    //         }
    //     }
    // }
    // results.push(await run("text.txt", "out.txt", 45, 8, 3, `${evalDir}/out`));

    const files = [
        // { snd: "json.h", rcv: "out.h" },
        // { snd: "text.txt", rcv: "out.txt" },
        { snd: "pic.bmp", rcv: "out.bmp" },
        // { snd: "beat.mp3", rcv: "out.mp3" },
    ];
    const configs = [
        // { snd: "-w 6", rcv: "-r 54" }, // minimum so that 2x rcv during 1x snd (rcv-window 1)
        // { snd: "-w 12", rcv: "-r 54" }, // minimum so that 2x rcv during 1x snd (rcv-window 2)
        // { snd: "-w 16", rcv: "" }, // minimum so that 2x rcv during 1x snd
        // { snd: "-w 16", rcv: "-r 54" },
        // { snd: "-w 60" },
        // { snd: "-w 90" },
        { snd: "-w 200" },
    ];

    for ({ snd: sndFile, rcv: rcvFile } of files) {
        for ({ snd: sndArgs, rcv: rcvArgs } of configs) {
            results.push(await run(sndFile, rcvFile, sndArgs, rcvArgs, `${evalDir}/out`));
        }
    }

    // results.push(await run("json.h", "out.h", 15, 8, 4, `${evalDir}/out`));
    // results.push(await run("text.txt", "out.txt", 15, 8, 4, `${evalDir}/out`));
    // results.push(await run("text.txt", "out.txt", 14, 8, 4, `${evalDir}/out`));
    // results.push(await run("pic.bmp", "out.bmp", 19, 8, 3, `${evalDir}/out`));
    // results.push(await run("text.txt", "out.txt", 28, 9, 3, `${evalDir}/out`));
    // for (let sw = 23; sw <= 31; sw+=2) {
    //     results.push(await run("text.txt", "out.txt", sw, 8, 3, `${evalDir}/sw_${sw}`));
    // }
    results = results.filter((r) => !isNaN(r.bandwidth));
    // results.sort((r1, r2) => r1.bandwidth - r2.bandwidth);
    console.log(results.slice(0, 25)); //.map((r) => `${r.rcvThreshold} b: ${r.bandwidth} e: ${r.errorRate}`));

    await disconnect();
}

main();
