const node_ssh = require("node-ssh");
const util = require("util");
const fs = require("fs");
const readFile = util.promisify(fs.readFile);
const { writeFileSync, mkdirSync } = fs;
const exec = util.promisify(require("child_process").exec);

const verbose = process.argv.includes("-v");
const vm1address = "192.168.122.230";
const vm2address = "192.168.122.206";
const receiverTimeout = 5000; // ms
const payloadSize = 13; // bytes

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
const run = async (sndFile, rcvFile, sndWindow, destDir) => {
    console.log(`[sndFile: ${sndFile} | rcvFile: ${rcvFile} | sndWindow: ${sndWindow}]`);
    mkdirSync(destDir, { recursive: true });

    // execute binaries
    const killReceiver = setTimeout(() => vm1ssh.execCommand("pkill receiver"), receiverTimeout);
    let transferDuration = "timeout";
    try {
        const [r1, r2] = await Promise.all([
            vm1ssh.exec("./receiver", ["-o", rcvFile], { cwd: remoteDir }),
            vm2ssh.exec("./sender", ["-f", sndFile, "-w", sndWindow], { cwd: remoteDir }),
        ]);
        transferDuration = parseFloat(r1.match(/time: (.*) s/)[1]);
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
        // vm2ssh.getFile(`${destDir}/${sndFile}`, `${remoteDir}/${sndFile}`),
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
        sndFile, rcvFile, sndWindow,
        lostPackets, insertedPackets, transferDuration, correctPackets,
        sentPackets: sndPackets.length,
        receivedPackets: rcvPackets.length,
        sentBytes: sndPackets.length * payloadSize,
        receivedBytes: rcvPackets.length * payloadSize,
        bandwidth: ((correctPackets * payloadSize) / transferDuration) / 1000, // kB/s
        // rawBandwidth: ((rcvPackets.length * payloadSize) / transferDuration) / 1000, // kB/s
        errorRate: 1 - (correctPackets / sndPackets.length),
    };
    writeFileSync(`${destDir}/results.json`, JSON.stringify(results, null, 4));
    if (verbose) console.log("results:", results);
    console.log("results saved");

    return results;
};

// entry point
const main = async () => {
    await connect();
    await compileAndCopy();

    const results = [];
    for (let w = 10; w < 50; w += 10) {
        for (let r = 0; r < 2; r++) {
            results.push(await run("text.txt", "out.txt", w, `${evalDir}/w${w}_r${r}`));
        }
    }
    results.sort((r1, r2) => r2.bandwidth - r1.bandwidth);
    console.log(results.slice(0, 3));

    await disconnect();
}

main();
