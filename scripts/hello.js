const node_ssh = require("node-ssh");
const util = require("util");
const readFile = util.promisify(require("fs").readFile);
const exec = util.promisify(require("child_process").exec);

const verbose = process.argv.includes("-v");
const vm1address = "192.168.122.230";
const vm2address = "192.168.122.206";
const receiverTimeout = 1000; // ms

const projectDir = `${process.env["HOME"]}/tlbchannels`;
const senderDir = `${projectDir}/src/sender`;
const receiverDir = `${projectDir}/src/receiver`;
const evalDir = `${projectDir}/eval`;
const remoteDir = "/home/user";

// parse csv logfile
const parseLogfile = (csv) => {
    const rows = csv.split("\n").slice(0, -1).map((row) => row.split(","));
    const packets = rows.map(([start, end, data]) => ({ 
        start, end, 
        data: data.split(" ").slice(0, -1),
    }));
    return packets;
};

const run = async () => {
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

    // connect
    const vm1ssh = new node_ssh();
    const vm2ssh = new node_ssh();
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

    // copy binaries
    await Promise.all([
        vm1ssh.putFile(`${receiverDir}/receiver`, `${remoteDir}/receiver`),
        vm2ssh.putFile(`${senderDir}/sender`, `${remoteDir}/sender`),
    ]);
    console.log("binaries copied");

    // execute binaries
    const killReceiver = setTimeout(() => vm1ssh.execCommand("pkill receiver"), receiverTimeout);
    try {
        const [r1, r2] = await Promise.all([
            vm1ssh.exec("./receiver", ["-v", "-o", "out.txt"], { cwd: remoteDir }),
            vm2ssh.exec("./sender", ["-v", "-s", "Hello World Hello World Hello World"], { cwd: remoteDir }),
        ]);
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

    // retrieve logs
    await Promise.all([
        vm1ssh.getFile(`${evalDir}/rcv_packets_log.csv`, `${remoteDir}/packets_log.csv`),
        vm2ssh.getFile(`${evalDir}/snd_packets_log.csv`, `${remoteDir}/packets_log.csv`),
    ]);
    console.log("retrieved logs");

    // disconnect
    vm1ssh.dispose();
    vm2ssh.dispose();
    console.log("ssh disconnected");

    // read logs
    const [rcvLog, sndLog] = await Promise.all([
        readFile(`${evalDir}/snd_packets_log.csv`, "utf8"),
        readFile(`${evalDir}/rcv_packets_log.csv`, "utf8"),
    ]);
    const sndPackets = parseLogfile(sndLog);
    const rcvPackets = parseLogfile(rcvLog);

    // compare data
    

    // stats
    const stats = {
        sentPackets: sndPackets.length,
    };
    console.log(stats);
};

run();