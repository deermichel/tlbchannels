const node_ssh = require("node-ssh");
const util = require("util");
const fs = require("fs");
const { writeFileSync, mkdirSync, appendFileSync } = fs;
const exec = util.promisify(require("child_process").exec);

const verbose = process.argv.includes("-v");
const vm1address = "192.168.122.190";
const vm2address = "192.168.122.201";
const receiverTimeout = 40000; // ms

const projectDir = `${process.env["HOME"]}/tlbchannels`;
const binDir = `${projectDir}/bin`;
const srcDir = `${projectDir}/src`;
const evalDir = `${projectDir}/eval`;
const remoteDir = "/home/user";

let vm1ssh, vm2ssh;

// compile locally and copy binaries
const compileAndCopy = async (buildFlags) => {
    // compile
    const { stdout } = await exec(`make CFLAGS="${buildFlags.join(" ")}"`, { cwd: srcDir });
    if (verbose) console.log(stdout);
    console.log(`compiled (flags: ${buildFlags.join(" ")})`);

    // cleanup stale runs
    await Promise.all([
        vm1ssh.execCommand("pkill receiver"),
        vm2ssh.execCommand("pkill sender"),
    ]);

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
const run = async (sndFile, rcvFile, sndWindow, destDir, flags) => {
    mkdirSync(destDir, { recursive: true });

    // execute binaries
    const killReceiver = setTimeout(() => vm1ssh.execCommand("pkill receiver"), receiverTimeout);
    try {
        const [r1, r2] = await Promise.all([
            vm1ssh.exec("./receiver", ["-o", rcvFile], { cwd: remoteDir }),
            vm2ssh.exec("./sender", ["-f", sndFile, "-w", sndWindow], { cwd: remoteDir }),
        ]);
        const stdout = `--- flags ---\n${flags.join(" ")}\n--- receiver stdout ---\n${r1}\n--- sender stdout (sndWindow: ${sndWindow}) ---\n${r2}\n---\n`;
        writeFileSync(`${destDir}/result.txt`, stdout);
        if (verbose) console.log(stdout);
    } catch (error) {
        if (error.message === "Terminated") {
            console.log("error: receiver timed out");
        } else {
            console.log(error);
        }
        clearTimeout(killReceiver);
        writeFileSync(`${destDir}/error.txt`, error.toString());
        return;
    }
    clearTimeout(killReceiver);
    console.log("binaries executed");

    // retrieve artifacts
    await Promise.all([
        vm1ssh.getFile(`${destDir}/${rcvFile}`, `${remoteDir}/${rcvFile}`),
        vm2ssh.getFile(`${destDir}/${sndFile}`, `${remoteDir}/${sndFile}`),
    ]);
    if (flags.includes("-DRECORD_PACKETS")) await Promise.all([
        vm1ssh.getFile(`${destDir}/rcv_packets_log.csv`, `${remoteDir}/packets_log.csv`),
        vm2ssh.getFile(`${destDir}/snd_packets_log.csv`, `${remoteDir}/packets_log.csv`),
    ]);
    console.log("retrieved artifacts");

    // compare and save results
    const [c1, c2] = await Promise.all([
        exec(`node packetcompare.js "${destDir}/${sndFile}" "${destDir}/${rcvFile}"`),
        exec(`node bytecompare.js "${destDir}/${sndFile}" "${destDir}/${rcvFile}"`),
    ]);
    const results = `--- packetcompare ---\n${c1.stdout}\n--- bytecompare ---\n${c2.stdout}\n---\n`;
    appendFileSync(`${destDir}/result.txt`, results);
    if (verbose) console.log(results);
    console.log("saved results");
    return results;
};

// entry point
const main = async () => {
    await connect();

    const iterations = 1;
    // rdtsc threshold: i7-broadwell 67, xeon-skylake 54
    // rdtsc window: i7-broadwell 1
    // num evictions (rdtsc): i7-broadwell 10
    const commonFlags = [ "-DARCH_BROADWELL", "-DRDTSC_THRESHOLD=67", "-DRDTSC_WINDOW=1", "-DRECORD_PACKETS" ];
    let configs = [
        {
            buildFlags: [`-DNUM_EVICTIONS=10 -DCHK_CRC8`],
            sndWindows: [50],
        },
    ];
    const files = [
        { sndFile: "json.h", rcvFile: "out.h" },
        // { sndFile: "sender.c", rcvFile: "out.c" },
        // { sndFile: "pic.png", rcvFile: "out.png" },
        // { sndFile: "pic.bmp", rcvFile: "out.bmp" },
        { sndFile: "beat.mp3", rcvFile: "out.mp3" },
    ];

    let results = [];
    for (let i = 0; i < iterations; i++) {
        for ({ buildFlags, sndWindows } of configs) {
            const allFlags = commonFlags.concat(buildFlags);
            await compileAndCopy(allFlags);
            for (sndWindow of sndWindows) {
                for ({ sndFile, rcvFile } of files) {
                    console.log("\nrun:", buildFlags, sndWindow, sndFile, rcvFile);
                    const outDir = `${evalDir}/${commonFlags}/iter_${i}/${buildFlags}/${sndWindow}/${sndFile}`;

                    const output = await run(sndFile, rcvFile, sndWindow, outDir, allFlags);
                    // console.log(output);
                    if (output) {
                        const metric = output.match(/received: (.*),/)[1];
                        const metric2 = output.match(/packetCorrectness: (.*),/)[1];
                        console.log({ receivedPackets: metric, correctness: metric2, outDir });
                        results.push({ receivedPackets: metric, correctness: metric2, outDir });
                    }
                }
            }
        }
    }

    console.log(results);

    await disconnect();
}

main();
