const node_ssh = require("node-ssh");
const util = require("util");
const fs = require("fs");
const { writeFileSync, mkdirSync, appendFileSync } = fs;
const exec = util.promisify(require("child_process").exec);

const verbose = process.argv.includes("-v");
const vm1address = "192.168.122.84";
const vm2address = "192.168.122.63";
const vm3address = "192.168.122.85";
const receiverTimeout = 600000; // ms

const projectDir = `${process.env["HOME"]}/tlbchannels`;
const binDir = `${projectDir}/bin`;
const srcDir = `${projectDir}/src`;
const evalDir = `${projectDir}/eval`;
const remoteDir = "/home/user";

let vm1ssh, vm2ssh, vm3ssh;

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
    vm3ssh = new node_ssh();
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
        vm3ssh.connect({
            host: vm3address,
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
    vm3ssh.dispose();
    console.log("ssh disconnected");
}

// run config and evaluate results
const run = async (sndFile, rcvFile, sndWindow, runParallel, destDir, flags) => {
    mkdirSync(destDir, { recursive: true });

    // run parallel
    if (runParallel.host) {
        console.log("run parallel on host:", runParallel.host[0]);
        exec(runParallel.host[0]).catch(() => {});
    }
    if (runParallel.vm1) {
        console.log("run parallel on vm1:", runParallel.vm1[0]);
        vm1ssh.execCommand(runParallel.vm1[0]);
    }
    if (runParallel.vm2) {
        console.log("run parallel on vm2:", runParallel.vm2[0]);
        vm2ssh.execCommand(runParallel.vm2[0]);
    }
    if (runParallel.vm3) {
        console.log("run parallel on vm3:", runParallel.vm3[0]);
        vm3ssh.execCommand(runParallel.vm3[0]);
    }
    if (runParallel.sleep) await exec(`sleep ${runParallel.sleep}`); // optional sleep for benchmark startup

    // execute binaries
    const killReceiver = setTimeout(() => vm1ssh.execCommand("pkill receiver"), receiverTimeout);
    try {
        // run covert channel
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

        // stop parallel runs
        if (runParallel.host) await exec(runParallel.host[1]);
        if (runParallel.vm1) await vm1ssh.execCommand(runParallel.vm1[1]);
        if (runParallel.vm2) await vm2ssh.execCommand(runParallel.vm2[1]);
        if (runParallel.vm3) await vm3ssh.execCommand(runParallel.vm3[1]);

        return;
    }
    clearTimeout(killReceiver);
    console.log("binaries executed");

    // stop parallel runs
    if (runParallel.host) await exec(runParallel.host[1]);
    if (runParallel.vm1) await vm1ssh.execCommand(runParallel.vm1[1]);
    if (runParallel.vm2) await vm2ssh.execCommand(runParallel.vm2[1]);
    if (runParallel.vm3) await vm3ssh.execCommand(runParallel.vm3[1]);

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

    const scenarios = {
        idle: {},
    };
    const files = [
        { sndFile: "genesis.txt", rcvFile: "out.txt" },
        // { sndFile: "pic.bmp", rcvFile: "out.bmp" },
    ];
    const configs = [
        { check: ["custom", "-DCHK_CUSTOM"], evict: 8, rs: 0, sndWindow: 120 },
    ];
    // const checksums = [["crc8", "-DCHK_CRC8"], ["berger", "-DCHK_BERGER"], ["custom", "-DCHK_CUSTOM"]];
    // const evictions = [8,9,7];
    // const sndWindows = [80,100,120,240,480,720,960,1280];
    // const rss = [0,32,64,96];

    let notFinished = [];
    for (let scen in scenarios) {
        for ({ sndFile, rcvFile } of files) {
            for (config of configs) {
                for (let iter = 0; iter < 10; iter++) {
                    const { evict, rs, sndWindow, tsc } = config;
                    const [check] = config.check;
                    const configstr = `${tsc ? "tsc-" : ""}${scen},${evict},${check},${sndFile},${sndWindow},${rs},${iter}`;
                    if (!fs.existsSync(`${evalDir}/ab04/${configstr}/finish.txt`)) {
                        notFinished.push(configstr);
                    }
                }
            }
        }
    }
    console.log("not finished:", notFinished.length);

    for (let scen in scenarios) {
        for ({ sndFile, rcvFile } of files) {
            for (config of configs) {
                for (let iter = 0; iter < 10; iter++) {
                    const { evict, rs, sndWindow, tsc } = config;
                    const [check, checkFlag] = config.check;
                    const configstr = `${tsc ? "tsc-" : ""}${scen},${evict},${check},${sndFile},${sndWindow},${rs},${iter}`;
                    if (notFinished.includes(configstr)) {
                        console.log(configstr);
                        const outDir = `${evalDir}/ab04/${configstr}`;
                        const buildFlags = ["-DARCH_BROADWELL", "-DRECORD_PACKETS", `-DNUM_EVICTIONS=${evict}`, checkFlag];
                        if (tsc) buildFlags.push("-DRDTSC_THRESHOLD=74", "-DRDTSC_WINDOW=2")
                        if (rs > 0) buildFlags.push(`-DREED_SOLOMON=${rs}`);
                        await compileAndCopy(buildFlags);
                        await run(sndFile, rcvFile, sndWindow, scenarios[scen], outDir, buildFlags);
                        writeFileSync(`${outDir}/finish.txt`, new Date().toISOString());
                    }
                }
            }
        }
    }

    await disconnect();
}

main();