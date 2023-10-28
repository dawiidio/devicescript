import { readFile } from "node:fs/promises"
import { join } from "node:path"
import { spawn } from "node:child_process"
import { verboseLog } from "./command"

interface PkgJson {
    dependencies: Record<string, string>
}

export const isPackageInstalledLocally = async (pkgName: string): Promise<boolean> => {
    const pkgJsonString = await readFile(join(process.cwd(), "package.json"), { encoding: "utf-8" })
    const pkgJson = JSON.parse(pkgJsonString) as PkgJson

    return Object.keys(pkgJson.dependencies).includes(pkgName)
}

const booleanAsk = async (question: string, defValue = true): Promise<boolean> => {
    let resolve: (val: boolean) => void
    const defValueString = defValue ? "(Y/n)" : "(y/N)"

    const ask = (q: string) => {
        process.stdout.write(`${q} ${defValueString}?`)
    }

    process.stdin.on("data", (data) => {
        const response = data.toString().trim().toLowerCase()

        if (response === "y") {
            resolve(true)
        } else if (response === "n") {
            process.stdin.end()
            resolve(false)
        } else if (response === '') {
            if (defValue)
                resolve(true)
            else
                resolve(false)
        }
        else {
            ask(`Unknown option: ${response}. Use y/n and try again \n${question}`);
        }
    })

    ask(question);

    return new Promise((res) => {
        resolve = res
    });
}

export const spawnAsyncInstaller = (packageName: string) => {
    let resolve: () => void
    let reject: () => void

    const installProcess = spawn('npm', ['install', '--save', '--no-workspaces', packageName], {
        cwd: process.cwd(),
        env: process.env,
    });

    installProcess.stderr.on('data', (data) => {
        verboseLog(`package installer process: ${data}`);
    });

    installProcess.on('close', (code) => {
        verboseLog(`install process exit with code ${code}`);

        if (code === 0)
            resolve();
        else
            reject();
    });

    return new Promise((res, rej) => {
        // @ts-ignore
        resolve = res
        reject = rej
    });
};

export const askForPackageInstallation = async (pkgName: string, installByDefault = true) => {
    if (await isPackageInstalledLocally(pkgName))
        return

    let shouldInstallPackage = await booleanAsk(
        `Install package "${pkgName}"`,
        installByDefault
    );

    if (shouldInstallPackage) {
        try {
            console.log(`Installing package "${pkgName}" ...`)
            await spawnAsyncInstaller(pkgName);
            console.log(`Package "${pkgName}" installed!`)
        }
        catch {
            console.log(`Automatic package installation failed :( You can try to install it manually by running "npm install --save ${pkgName}"`)
        }
    }
}