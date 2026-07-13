const vscode = require('vscode');
const { spawn } = require('child_process');
const path = require('path');

function activate(context) {
  let outputChannel = vscode.window.createOutputChannel('PT');

  let runCommand = vscode.commands.registerCommand('pt.run', () => {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
      vscode.window.showErrorMessage('No active file');
      return;
    }

    const filePath = editor.document.fileName;
    if (!filePath.endsWith('.pt')) {
      vscode.window.showErrorMessage('Not a .pt file');
      return;
    }

    // Save the file first
    editor.document.save().then(() => {
      outputChannel.clear();
      outputChannel.show(true);

      const config = vscode.workspace.getConfiguration('pt');
      const executablePath = config.get('executablePath', 'pt');

      outputChannel.appendLine(`> ${executablePath} ${filePath}`);
      outputChannel.appendLine('');

      const proc = spawn(executablePath, [filePath], {
        cwd: path.dirname(filePath),
        env: process.env
      });

      proc.stdout.on('data', (data) => {
        outputChannel.append(data.toString());
      });

      proc.stderr.on('data', (data) => {
        outputChannel.appendLine(data.toString());
      });

      proc.on('error', (err) => {
        if (err.code === 'ENOENT') {
          outputChannel.appendLine(`Error: 'pt' not found. Install PT and make sure it's in your PATH.`);
          outputChannel.appendLine('');
          outputChannel.appendLine('macOS/Linux: curl -sSL https://raw.githubusercontent.com/phyothant-dev/PT-Programming-Language/main/install.sh | sh');
          outputChannel.appendLine('Windows: Download from https://github.com/phyothant-dev/PT-Programming-Language/releases');
        } else {
          outputChannel.appendLine(`Error: ${err.message}`);
        }
      });

      proc.on('close', (code) => {
        outputChannel.appendLine('');
        outputChannel.appendLine(`Process exited with code ${code}`);
      });
    });
  });

  context.subscriptions.push(runCommand);
}

function deactivate() {}

module.exports = { activate, deactivate };
