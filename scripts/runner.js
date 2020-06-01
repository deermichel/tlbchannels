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
const binDir = `${projectDir}/bin`;
const srcDir = `${projectDir}/src`;
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
const compileAndCopy = async (buildFlags) => {
    // compile
    const { stdout } = await exec(`make CFLAGS="${buildFlags.join(" ")}"`, { cwd: srcDir });
    if (verbose) console.log(stdout);
    console.log(`compiled (flags: ${buildFlags.join(" ")})`);

    // copy binaries
    await Promise.all([
        vm1ssh.putFile(`${binDir}/receiver`, `${remoteDir}/receiver`),
        vm2ssh.putFile(`${binDir}/sender`, `${remoteDir}/sender`),
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

    let results = [];
    const commonFlags = [ "-DARCH_BROADWELL", "-DNUM_EVICTIONS=23" ];
    const configs = [
        // { snd: "-w 6", rcv: "-r 54" }, // minimum so that 2x rcv during 1x snd (rcv-window 1)
        // { snd: "-w 12", rcv: "-r 54" }, // minimum so that 2x rcv during 1x snd (rcv-window 2)
        // { snd: "-w 16", rcv: "" }, // minimum so that 2x rcv during 1x snd
        {
            buildFlags: ["-DAS"], 
            sndWindows: [12, 43, 5],
        },
        {
            buildFlags: ["-DF"],
            sndWindows: [12, 23, 5],
        }
    ];
    const files = [
        // { sndFile: "json.h", rcvFile: "out.h" },
        { sndFile: "text.txt", rcvFile: "out.txt" },
        { sndFile: "pic.bmp", rcvFile: "out.bmp" },
        { sndFile: "beat.mp3", rcvFile: "out.mp3" },
    ];

    for ({ buildFlags, sndWindows } of configs) {
        await compileAndCopy(commonFlags.concat(buildFlags));
        for (sndWindow of sndWindows) {
            for ({ sndFile, rcvFile } of files) {
                console.log(commonFlags.concat(buildFlags), sndWindow, sndFile, rcvFile);
                // results.push(await run(sndFile, rcvFile, sndArgs, rcvArgs, `${evalDir}/out`));
            }
        }
    }

    await disconnect();
}

main();
