using System;
using System.Diagnostics;
using System.Drawing;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Microsoft.Win32;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Windows.Forms;

namespace Rott94CoopLauncher
{
    internal static class Program
    {
        [STAThread]
        private static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new LauncherForm());
        }
    }

    internal sealed class LauncherForm : Form
    {
        private const string GameExe = "Rott94.Windows.Release.exe";
        private static readonly Color Background = Color.FromArgb(22, 18, 15);
        private static readonly Color Card = Color.FromArgb(43, 35, 29);
        private static readonly Color Gold = Color.FromArgb(226, 166, 53);
        private static readonly Color Cream = Color.FromArgb(232, 220, 190);
        private static readonly Color Red = Color.FromArgb(151, 42, 31);
        private static readonly Color Green = Color.FromArgb(72, 121, 68);

        private readonly TextBox joinAddress;
        private readonly CheckBox windowed;
        private readonly CheckBox skipIntro;
        private readonly Label status;
        private readonly string localAddress;
        private readonly List<Process> launchedProcesses = new List<Process>();
        private readonly object diagnosticsLock = new object();

        public LauncherForm()
        {
            localAddress = FindLanAddress();

            Text = "ROTT94 Co-op Launcher";
            ClientSize = new Size(520, 432);
            FormBorderStyle = FormBorderStyle.FixedSingle;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = Background;
            ForeColor = Cream;
            Font = new Font("Segoe UI", 9F);

            Controls.Add(MakeLabel("▲  ROTT94 CO-OP", 22, 18, 476, 38, Gold, 20F, FontStyle.Bold));
            Controls.Add(MakeLabel("Two players. One campaign. Ludicrous teamwork.", 24, 57, 470, 24,
                Color.FromArgb(180, 170, 145), 9.5F, FontStyle.Regular));

            Panel hostCard = MakeCard(20, 92, 480, 92);
            hostCard.Controls.Add(MakeLabel("HOST A GAME", 16, 12, 175, 24, Gold, 11F, FontStyle.Bold));
            hostCard.Controls.Add(MakeLabel("Your LAN address:", 16, 42, 115, 22, Cream, 9F, FontStyle.Regular));
            Label address = MakeLabel(localAddress, 132, 40, 170, 25, Color.White, 10.5F, FontStyle.Bold);
            hostCard.Controls.Add(address);
            Button copy = MakeButton("COPY IP", 305, 34, 72, 32, Card, Gold);
            copy.Click += delegate { Clipboard.SetText(localAddress); SetStatus("LAN address copied."); };
            hostCard.Controls.Add(copy);
            Button host = MakeButton("HOST", 386, 20, 78, 52, Green, Color.White);
            host.Click += delegate { LaunchHost(); };
            hostCard.Controls.Add(host);
            Controls.Add(hostCard);

            Panel joinCard = MakeCard(20, 196, 480, 92);
            joinCard.Controls.Add(MakeLabel("JOIN A GAME", 16, 12, 175, 24, Gold, 11F, FontStyle.Bold));
            joinAddress = new TextBox
            {
                Location = new Point(16, 43),
                Size = new Size(350, 25),
                BackColor = Color.FromArgb(28, 24, 21),
                ForeColor = Color.White,
                BorderStyle = BorderStyle.FixedSingle,
                Font = new Font("Consolas", 10F),
                Text = "192.168.1."
            };
            joinAddress.KeyDown += delegate(object sender, KeyEventArgs e)
            {
                if (e.KeyCode == Keys.Enter) LaunchClient();
            };
            joinCard.Controls.Add(joinAddress);
            Button join = MakeButton("JOIN", 386, 20, 78, 52, Red, Color.White);
            join.Click += delegate { LaunchClient(); };
            joinCard.Controls.Add(join);
            Controls.Add(joinCard);

            Button singlePlayer = MakeButton("SINGLE PLAYER", 20, 304, 145, 40, Gold, Background);
            singlePlayer.Click += delegate { LaunchSinglePlayer(); };
            Controls.Add(singlePlayer);

            windowed = MakeCheckBox("Windowed", 184, 312, true);
            skipIntro = MakeCheckBox("Skip intro", 290, 312, true);
            Controls.Add(windowed);
            Controls.Add(skipIntro);
            Controls.Add(MakeLabel("Co-op: 2 players  •  UDP 34858  •  matching builds", 24, 351, 472, 22,
                Color.FromArgb(155, 145, 125), 8.5F, FontStyle.Regular));

            status = MakeLabel("Ready. Allow the game through Windows Firewall on Private networks.",
                24, 389, 472, 25, Cream, 8.8F, FontStyle.Regular);
            Controls.Add(status);
        }

        private void LaunchSinglePlayer()
        {
            if (LaunchGame(CommonArguments().Trim()))
                SetStatus("Single-player started.");
        }

        private void LaunchHost()
        {
            if (LaunchGame("-net -server -players 2" + CommonArguments()))
                SetStatus("Host started. Give your friend " + localAddress + " and wait before choosing Co-op Campaign.");
        }

        private void LaunchClient()
        {
            IPAddress parsed;
            string address = joinAddress.Text.Trim();
            if (!IPAddress.TryParse(address, out parsed) || parsed.AddressFamily != AddressFamily.InterNetwork)
            {
                MessageBox.Show(this, "Enter the host's IPv4 address, for example 192.168.1.42.",
                    "Invalid host address", MessageBoxButtons.OK, MessageBoxIcon.Information);
                joinAddress.Focus();
                return;
            }

            if (LaunchGame("-net " + address + CommonArguments()))
                SetStatus("Joining " + address + "…");
        }

        private string CommonArguments()
        {
            // Always override config.rot: merely omitting -window leaves the last
            // saved FullScreen value in control, which makes this checkbox appear
            // to do nothing after a windowed run.
            string args = windowed.Checked ? " -window" : " -fullscreen";
            if (skipIntro.Checked) args += " -nowait";
            return args;
        }

        private bool LaunchGame(string arguments)
        {
            string folder = Application.StartupPath;
            string game = Path.Combine(folder, GameExe);
            if (!File.Exists(game))
            {
                MessageBox.Show(this,
                    "Place Rott94CoopLauncher.exe beside " + GameExe + ".\n\nExpected:\n" + game,
                    "Game not found", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return false;
            }

            if (!EnsureGameData(folder))
                return false;

            try
            {
                string diagnostics = Path.Combine(folder, "diagnostics");
                Directory.CreateDirectory(diagnostics);
                ConfigureCrashDumps(diagnostics);

                Process process = Process.Start(new ProcessStartInfo(game, arguments)
                {
                    WorkingDirectory = folder,
                    UseShellExecute = true
                });
                if (process == null)
                    throw new InvalidOperationException("Windows did not return a game process.");

                process.EnableRaisingEvents = true;
                launchedProcesses.Add(process);
                AppendDiagnostic(diagnostics, "Started PID " + process.Id + ": " + arguments);
                process.Exited += delegate
                {
                    int exitCode;
                    try { exitCode = process.ExitCode; }
                    catch { exitCode = int.MinValue; }
                    AppendDiagnostic(diagnostics, "PID " + process.Id + " exited with code " + exitCode + ".");
                    lock (diagnosticsLock) { launchedProcesses.Remove(process); }
                    process.Dispose();
                };
                return true;
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, ex.Message, "Could not start ROTT94",
                    MessageBoxButtons.OK, MessageBoxIcon.Error);
                return false;
            }
        }

        private void ConfigureCrashDumps(string diagnostics)
        {
            string dumpFolder = Path.Combine(diagnostics, "crash-dumps");
            Directory.CreateDirectory(dumpFolder);

            try
            {
                using (RegistryKey key = Registry.CurrentUser.CreateSubKey(
                    @"Software\Microsoft\Windows\Windows Error Reporting\LocalDumps\" + GameExe))
                {
                    key.SetValue("DumpFolder", dumpFolder, RegistryValueKind.ExpandString);
                    key.SetValue("DumpCount", 10, RegistryValueKind.DWord);
                    key.SetValue("DumpType", 2, RegistryValueKind.DWord);
                }
            }
            catch (Exception ex)
            {
                AppendDiagnostic(diagnostics, "Could not enable Windows crash dumps: " + ex.Message);
            }
        }

        private void AppendDiagnostic(string diagnostics, string message)
        {
            try
            {
                lock (diagnosticsLock)
                {
                    File.AppendAllText(Path.Combine(diagnostics, "launcher-session.log"),
                        DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + "  " + message + Environment.NewLine);
                }
            }
            catch
            {
                // Diagnostics must never prevent the game from launching or exiting.
            }
        }

        private bool EnsureGameData(string gameFolder)
        {
            string destination = System.IO.Path.Combine(gameFolder, "assets");
            string destinationWad = System.IO.Path.Combine(destination, "darkwar.wad");
            if (System.IO.File.Exists(destinationWad))
                return true;

            string steamAssets = System.IO.Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
                "Steam", "steamapps", "common", "Rise of the Triad - Ludicrous Edition", "assets");
            string source = steamAssets;

            if (!System.IO.File.Exists(System.IO.Path.Combine(source, "darkwar.wad")))
            {
                using (FolderBrowserDialog picker = new FolderBrowserDialog())
                {
                    picker.Description = "Select the Ludicrous Edition assets folder containing DARKWAR.WAD.";
                    picker.ShowNewFolderButton = false;
                    if (picker.ShowDialog(this) != DialogResult.OK)
                        return false;
                    source = picker.SelectedPath;
                }
            }

            if (!System.IO.File.Exists(System.IO.Path.Combine(source, "darkwar.wad")))
            {
                MessageBox.Show(this, "DARKWAR.WAD was not found in the selected folder.",
                    "Game data not found", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return false;
            }

            try
            {
                System.IO.Directory.CreateDirectory(destination);
                string[] files = { "darkwar.wad", "darkwar.rtl", "remote1.rts" };
                foreach (string file in files)
                {
                    string sourceFile = System.IO.Path.Combine(source, file);
                    if (System.IO.File.Exists(sourceFile))
                        System.IO.File.Copy(sourceFile, System.IO.Path.Combine(destination, file), true);
                }
                SetStatus("Registered game data imported from the Ludicrous Edition.");
                return true;
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, "Could not copy the game data:\n\n" + ex.Message,
                    "Import failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return false;
            }
        }

        private void SetStatus(string text)
        {
            status.Text = text;
        }

        private static string FindLanAddress()
        {
            try
            {
                var candidates = NetworkInterface.GetAllNetworkInterfaces()
                    .Where(n => n.OperationalStatus == OperationalStatus.Up &&
                                n.NetworkInterfaceType != NetworkInterfaceType.Loopback)
                    .SelectMany(n => n.GetIPProperties().UnicastAddresses)
                    .Select(a => a.Address)
                    .Where(a => a.AddressFamily == AddressFamily.InterNetwork && !IPAddress.IsLoopback(a))
                    .ToArray();

                IPAddress privateAddress = candidates.FirstOrDefault(a =>
                    a.GetAddressBytes()[0] == 10 ||
                    (a.GetAddressBytes()[0] == 172 && a.GetAddressBytes()[1] >= 16 && a.GetAddressBytes()[1] <= 31) ||
                    (a.GetAddressBytes()[0] == 192 && a.GetAddressBytes()[1] == 168));

                return (privateAddress ?? candidates.FirstOrDefault() ?? IPAddress.Loopback).ToString();
            }
            catch
            {
                return "127.0.0.1";
            }
        }

        private static Panel MakeCard(int x, int y, int width, int height)
        {
            return new Panel { Location = new Point(x, y), Size = new Size(width, height), BackColor = Card };
        }

        private static Label MakeLabel(string text, int x, int y, int width, int height,
            Color color, float size, FontStyle style)
        {
            return new Label
            {
                Text = text,
                Location = new Point(x, y),
                Size = new Size(width, height),
                ForeColor = color,
                Font = new Font("Segoe UI", size, style),
                TextAlign = ContentAlignment.MiddleLeft
            };
        }

        private static Button MakeButton(string text, int x, int y, int width, int height,
            Color back, Color fore)
        {
            Button button = new Button
            {
                Text = text,
                Location = new Point(x, y),
                Size = new Size(width, height),
                BackColor = back,
                ForeColor = fore,
                FlatStyle = FlatStyle.Flat,
                Font = new Font("Segoe UI", 9F, FontStyle.Bold),
                Cursor = Cursors.Hand
            };
            button.FlatAppearance.BorderColor = Gold;
            button.FlatAppearance.BorderSize = 1;
            return button;
        }

        private static CheckBox MakeCheckBox(string text, int x, int y, bool value)
        {
            return new CheckBox
            {
                Text = text,
                Location = new Point(x, y),
                Size = new Size(100, 23),
                Checked = value,
                ForeColor = Cream,
                BackColor = Background
            };
        }
    }
}
