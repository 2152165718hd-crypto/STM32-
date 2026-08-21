using System;
using System.Collections.Generic;
using System.Drawing;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows.Forms;

namespace PC_TcpUpper
{
    internal sealed class MainForm : Form
    {
        private readonly object _sendLock = new object();
        private readonly object _writeLock = new object();
        private readonly object _frameQueueLock = new object();
        private readonly List<byte> _rxBuffer = new List<byte>();
        private readonly Queue<TcpFrame> _pendingFrames = new Queue<TcpFrame>();
        private readonly JavaScriptSerializer _json = new JavaScriptSerializer();
        private readonly System.Windows.Forms.Timer _heartbeatTimer = new System.Windows.Forms.Timer();
        private readonly System.Windows.Forms.Timer _staleTimer = new System.Windows.Forms.Timer();
        private readonly System.Windows.Forms.Timer _framePumpTimer = new System.Windows.Forms.Timer();
        private const int MaxLogTextLength = 60000;
        private const int MaxQueuedFrames = 512;
        private const int MaxFramesPerUiPump = 80;
        private const double DataStaleProbeSeconds = 6.0;
        private const double DataStaleReconnectSeconds = 30.0;
        private const double LoginRetrySeconds = 2.0;
        private const double LoginReconnectSeconds = 20.0;
        private const int MinSendGapMilliseconds = 160;
        private const double RecoveryProbeIntervalSeconds = 5.0;
        private DateTime _lastRawRealtimeLog = DateTime.MinValue;
        private DateTime _lastLiveRealtimeLog = DateTime.MinValue;
        private DateTime _lastFrameAt = DateTime.MinValue;
        private DateTime _lastDataFrameAt = DateTime.MinValue;
        private DateTime _lastRecoveryProbeAt = DateTime.MinValue;
        private DateTime _connectedAt = DateTime.MinValue;
        private DateTime _lastLoginSentAt = DateTime.MinValue;
        private DateTime _lastTxAt = DateTime.MinValue;
        private bool _loginReceived;
        private int _loginAttemptCount;
        private bool _dataStale;
        private bool _autoReconnectInProgress;
        private string _connectedHost = string.Empty;
        private int _connectedPort;
        private int _connectionGeneration;

        private readonly Color _pageBackColor = Color.FromArgb(246, 248, 251);
        private readonly Color _panelBackColor = Color.White;
        private readonly Color _borderColor = Color.FromArgb(224, 229, 236);
        private readonly Color _textColor = Color.FromArgb(29, 39, 54);
        private readonly Color _mutedTextColor = Color.FromArgb(101, 116, 139);
        private readonly Color _primaryColor = Color.FromArgb(37, 99, 235);
        private readonly Color _successColor = Color.FromArgb(22, 163, 74);
        private readonly Color _warningColor = Color.FromArgb(217, 119, 6);
        private readonly Color _dangerColor = Color.FromArgb(220, 38, 38);
        private readonly Color _offlineColor = Color.FromArgb(100, 116, 139);

        private TextBox _hostText;
        private TextBox _portText;
        private Button _connectButton;
        private Button _disconnectButton;
        private Label _connectionDotLabel;
        private Label _connectionLabel;
        private Label _deviceLabel;
        private Label _tickLabel;
        private Label _stateLabel;
        private Label _armedLabel;
        private Label _alarmStatusLabel;
        private Label _alarmReasonLabel;
        private Label _clientLabel;
        private Label _audioValueLabel;
        private Label _audioMetaLabel;
        private Label _audioEnergyLabel;
        private Label _vibrationValueLabel;
        private Label _vibrationMetaLabel;
        private Label _vibrationEnergyLabel;
        private Label _lastUpdateLabel;
        private NumericUpDown _alarmHoldInput;
        private NumericUpDown _fusionInput;
        private NumericUpDown _audioMediumInput;
        private NumericUpDown _audioStrongInput;
        private TextBox _liveLogText;
        private TextBox _alarmLogText;
        private TextBox _historyText;
        private TextBox _rawLogText;
        private Panel _stateCard;
        private Panel _alarmCard;
        private Panel _audioCard;
        private Panel _vibrationCard;
        private Panel _connectionCard;
        private TableLayoutPanel _mainGrid;
        private TabControl _logTabs;

        private TcpClient _client;
        private NetworkStream _stream;
        private Thread _rxThread;
        private uint _sequence = 1;
        private volatile bool _closing;

        public MainForm()
        {
            Text = "家庭安防 TCP 上位机";
            StartPosition = FormStartPosition.CenterScreen;
            MinimumSize = new Size(1120, 720);
            Size = new Size(1280, 780);
            Font = new Font("Microsoft YaHei UI", 9f);
            BackColor = _pageBackColor;

            BuildUi();

            _heartbeatTimer.Interval = 8000;
            _heartbeatTimer.Tick += HeartbeatTimer_Tick;
            _staleTimer.Interval = 1000;
            _staleTimer.Tick += StaleTimer_Tick;
            _framePumpTimer.Interval = 50;
            _framePumpTimer.Tick += FramePumpTimer_Tick;
            UpdateConnectionUi(false);
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            DisconnectCore(false);
            base.OnFormClosing(e);
        }

        private void BuildUi()
        {
            TableLayoutPanel root = new TableLayoutPanel();
            root.Dock = DockStyle.Fill;
            root.BackColor = _pageBackColor;
            root.RowCount = 2;
            root.ColumnCount = 1;
            root.Padding = new Padding(18);
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 68));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            Controls.Add(root);

            root.Controls.Add(BuildTopBar(), 0, 0);

            _mainGrid = new TableLayoutPanel();
            _mainGrid.Dock = DockStyle.Fill;
            _mainGrid.BackColor = _pageBackColor;
            _mainGrid.ColumnCount = 2;
            _mainGrid.RowCount = 1;
            _mainGrid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 62));
            _mainGrid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 38));
            root.Controls.Add(_mainGrid, 0, 1);

            _mainGrid.Controls.Add(BuildDashboard(), 0, 0);
            _mainGrid.Controls.Add(BuildSidePanel(), 1, 0);
        }

        private Control BuildTopBar()
        {
            CleanPanel bar = new CleanPanel(_borderColor);
            bar.Dock = DockStyle.Fill;
            bar.BackColor = _panelBackColor;
            bar.Padding = new Padding(18, 10, 18, 10);
            bar.Margin = new Padding(0, 0, 0, 12);

            TableLayoutPanel layout = new TableLayoutPanel();
            layout.Dock = DockStyle.Fill;
            layout.BackColor = _panelBackColor;
            layout.ColumnCount = 2;
            layout.RowCount = 1;
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 48));
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 52));
            bar.Controls.Add(layout);

            TableLayoutPanel titleArea = new TableLayoutPanel();
            titleArea.Dock = DockStyle.Fill;
            titleArea.BackColor = _panelBackColor;
            titleArea.RowCount = 2;
            titleArea.ColumnCount = 1;
            titleArea.RowStyles.Add(new RowStyle(SizeType.Absolute, 26));
            titleArea.RowStyles.Add(new RowStyle(SizeType.Absolute, 20));
            layout.Controls.Add(titleArea, 0, 0);

            Label title = new Label();
            title.Text = "家庭安防预警系统";
            title.Dock = DockStyle.Fill;
            title.Font = new Font(Font.FontFamily, 14f, FontStyle.Bold);
            title.ForeColor = _textColor;
            title.TextAlign = ContentAlignment.MiddleLeft;
            titleArea.Controls.Add(title, 0, 0);

            _connectionLabel = new Label();
            _connectionLabel.Dock = DockStyle.Fill;
            _connectionLabel.ForeColor = _mutedTextColor;
            _connectionLabel.TextAlign = ContentAlignment.MiddleLeft;
            titleArea.Controls.Add(_connectionLabel, 0, 1);

            FlowLayoutPanel connectFlow = new FlowLayoutPanel();
            connectFlow.Dock = DockStyle.Fill;
            connectFlow.BackColor = _panelBackColor;
            connectFlow.WrapContents = false;
            connectFlow.FlowDirection = FlowDirection.LeftToRight;
            connectFlow.AutoScroll = true;
            connectFlow.Padding = new Padding(0, 5, 0, 0);
            layout.Controls.Add(connectFlow, 1, 0);

            connectFlow.Controls.Add(MakeLabel("地址", 34));
            _hostText = MakeTextBox("192.168.4.1", 132);
            connectFlow.Controls.Add(_hostText);

            connectFlow.Controls.Add(MakeLabel("端口", 34));
            _portText = MakeTextBox("5000", 68);
            connectFlow.Controls.Add(_portText);

            _connectButton = MakeButton("连接", _primaryColor);
            _connectButton.Width = 76;
            _connectButton.Click += ConnectButton_Click;
            connectFlow.Controls.Add(_connectButton);

            _disconnectButton = MakeButton("断开", Color.FromArgb(71, 85, 105));
            _disconnectButton.Width = 76;
            _disconnectButton.Click += delegate { DisconnectCore(true); };
            connectFlow.Controls.Add(_disconnectButton);

            return bar;
        }

        private Control BuildDashboard()
        {
            TableLayoutPanel dashboard = new TableLayoutPanel();
            dashboard.Dock = DockStyle.Fill;
            dashboard.BackColor = _pageBackColor;
            dashboard.RowCount = 4;
            dashboard.ColumnCount = 2;
            dashboard.Padding = new Padding(0, 0, 14, 0);
            dashboard.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
            dashboard.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
            dashboard.RowStyles.Add(new RowStyle(SizeType.Absolute, 132));
            dashboard.RowStyles.Add(new RowStyle(SizeType.Absolute, 164));
            dashboard.RowStyles.Add(new RowStyle(SizeType.Absolute, 164));
            dashboard.RowStyles.Add(new RowStyle(SizeType.Percent, 100));

            _stateCard = BuildStateCard();
            dashboard.Controls.Add(_stateCard, 0, 0);

            _alarmCard = BuildAlarmCard();
            dashboard.Controls.Add(_alarmCard, 1, 0);

            _audioCard = BuildSignalCard(
                "声音监测",
                "强度",
                out _audioValueLabel,
                out _audioMetaLabel,
                out _audioEnergyLabel);
            dashboard.Controls.Add(_audioCard, 0, 1);
            dashboard.SetColumnSpan(_audioCard, 2);

            _vibrationCard = BuildSignalCard(
                "振动监测",
                "强度",
                out _vibrationValueLabel,
                out _vibrationMetaLabel,
                out _vibrationEnergyLabel);
            dashboard.Controls.Add(_vibrationCard, 0, 2);
            dashboard.SetColumnSpan(_vibrationCard, 2);

            CleanPanel configPanel = new CleanPanel(_borderColor);
            configPanel.Dock = DockStyle.Fill;
            configPanel.BackColor = _panelBackColor;
            configPanel.Margin = new Padding(0, 14, 0, 0);
            configPanel.Padding = new Padding(16, 12, 16, 12);
            dashboard.Controls.Add(configPanel, 0, 3);
            dashboard.SetColumnSpan(configPanel, 2);

            TableLayoutPanel config = new TableLayoutPanel();
            config.Dock = DockStyle.Fill;
            config.BackColor = _panelBackColor;
            config.RowCount = 4;
            config.ColumnCount = 4;
            config.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 25));
            config.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 25));
            config.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 25));
            config.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 25));
            config.RowStyles.Add(new RowStyle(SizeType.Absolute, 26));
            config.RowStyles.Add(new RowStyle(SizeType.Absolute, 56));
            config.RowStyles.Add(new RowStyle(SizeType.Absolute, 46));
            config.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            configPanel.Controls.Add(config);

            Label configTitle = MakeSectionTitle("参数设置");
            config.Controls.Add(configTitle, 0, 0);
            config.SetColumnSpan(configTitle, 4);

            _alarmHoldInput = AddMetricNumeric(config, "报警保持", "ms", 3000, 60000, 15000, 0, 1);
            _fusionInput = AddMetricNumeric(config, "融合窗口", "ms", 100, 1000, 300, 1, 1);
            _audioMediumInput = AddMetricNumeric(config, "中等声音", "%", 20, 90, 40, 2, 1);
            _audioStrongInput = AddMetricNumeric(config, "强声音", "%", 30, 95, 55, 3, 1);

            TableLayoutPanel actions = new TableLayoutPanel();
            actions.Dock = DockStyle.Fill;
            actions.BackColor = _panelBackColor;
            actions.ColumnCount = 6;
            actions.RowCount = 1;
            for (int i = 0; i < 6; i++)
            {
                actions.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f / 6f));
            }
            config.Controls.Add(actions, 0, 2);
            config.SetColumnSpan(actions, 4);

            AddActionButton(actions, "布防", "arm", 0);
            AddActionButton(actions, "撤防", "disarm", 1);
            AddActionButton(actions, "静音", "silence", 2);
            AddActionButton(actions, "清除报警", "clear_alarm", 3);

            Button sendConfig = MakeOutlineButton("发送参数");
            sendConfig.Click += SendConfig_Click;
            actions.Controls.Add(sendConfig, 4, 0);

            Button queryStatus = MakeOutlineButton("刷新状态");
            queryStatus.Click += delegate { SendMessage(TcpMessageType.StatusQuery, "{}"); };
            actions.Controls.Add(queryStatus, 5, 0);

            return dashboard;
        }

        private Panel BuildStateCard()
        {
            CleanPanel card = CreateCard(0, 0, 7, 0);

            TableLayoutPanel layout = new TableLayoutPanel();
            layout.Dock = DockStyle.Fill;
            layout.BackColor = _panelBackColor;
            layout.RowCount = 4;
            layout.ColumnCount = 1;
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 26));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 44));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 26));
            layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            card.Controls.Add(layout);

            layout.Controls.Add(MakeCardCaption("系统状态"), 0, 0);

            _stateLabel = new Label();
            _stateLabel.Text = "-";
            _stateLabel.Dock = DockStyle.Fill;
            _stateLabel.Font = new Font(Font.FontFamily, 21f, FontStyle.Bold);
            _stateLabel.ForeColor = _offlineColor;
            _stateLabel.TextAlign = ContentAlignment.MiddleLeft;
            layout.Controls.Add(_stateLabel, 0, 1);

            _armedLabel = MakeMutedLabel("布防: -  静音: -");
            layout.Controls.Add(_armedLabel, 0, 2);

            _deviceLabel = MakeMutedLabel("设备: -");
            layout.Controls.Add(_deviceLabel, 0, 3);

            return card;
        }

        private Panel BuildAlarmCard()
        {
            CleanPanel card = CreateCard(7, 0, 0, 0);

            TableLayoutPanel layout = new TableLayoutPanel();
            layout.Dock = DockStyle.Fill;
            layout.BackColor = _panelBackColor;
            layout.RowCount = 4;
            layout.ColumnCount = 1;
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 26));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 44));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 26));
            layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            card.Controls.Add(layout);

            layout.Controls.Add(MakeCardCaption("报警信息"), 0, 0);

            _alarmStatusLabel = new Label();
            _alarmStatusLabel.Text = "正常";
            _alarmStatusLabel.Dock = DockStyle.Fill;
            _alarmStatusLabel.Font = new Font(Font.FontFamily, 21f, FontStyle.Bold);
            _alarmStatusLabel.ForeColor = _successColor;
            _alarmStatusLabel.TextAlign = ContentAlignment.MiddleLeft;
            layout.Controls.Add(_alarmStatusLabel, 0, 1);

            _alarmReasonLabel = MakeMutedLabel("原因: -");
            layout.Controls.Add(_alarmReasonLabel, 0, 2);

            _tickLabel = MakeMutedLabel("最后报警 tick: -");
            layout.Controls.Add(_tickLabel, 0, 3);

            return card;
        }

        private Panel BuildSignalCard(
            string titleText,
            string valueCaption,
            out Label valueLabel,
            out Label metaLabel,
            out Label energyLabel)
        {
            CleanPanel card = CreateCard(0, 14, 0, 0);
            card.Padding = new Padding(18, 14, 18, 12);

            TableLayoutPanel layout = new TableLayoutPanel();
            layout.Dock = DockStyle.Fill;
            layout.BackColor = _panelBackColor;
            layout.RowCount = 2;
            layout.ColumnCount = 3;
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 33.33f));
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 33.33f));
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 33.34f));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 28));
            layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            card.Controls.Add(layout);

            Label title = MakeSectionTitle(titleText);
            layout.Controls.Add(title, 0, 0);
            layout.SetColumnSpan(title, 3);

            valueLabel = AddMetricTile(layout, valueCaption, 0);
            metaLabel = AddMetricTile(layout, "频率", 1);
            energyLabel = AddMetricTile(layout, "能量", 2);

            return card;
        }

        private Label AddMetricTile(TableLayoutPanel parent, string captionText, int column)
        {
            TableLayoutPanel tile = new TableLayoutPanel();
            tile.Dock = DockStyle.Fill;
            tile.BackColor = _panelBackColor;
            tile.Margin = new Padding(column == 0 ? 0 : 12, 0, column == 2 ? 0 : 12, 0);
            tile.RowCount = 2;
            tile.ColumnCount = 1;
            tile.RowStyles.Add(new RowStyle(SizeType.Absolute, 26));
            tile.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            parent.Controls.Add(tile, column, 1);

            Label caption = MakeMutedLabel(captionText);
            tile.Controls.Add(caption, 0, 0);

            Label value = new Label();
            value.Text = "-";
            value.Dock = DockStyle.Fill;
            value.Font = new Font(Font.FontFamily, 20f, FontStyle.Bold);
            value.ForeColor = _textColor;
            value.TextAlign = ContentAlignment.MiddleLeft;
            value.AutoEllipsis = true;
            tile.Controls.Add(value, 0, 1);
            return value;
        }

        private Control BuildSidePanel()
        {
            TableLayoutPanel side = new TableLayoutPanel();
            side.Dock = DockStyle.Fill;
            side.BackColor = _pageBackColor;
            side.RowCount = 3;
            side.ColumnCount = 1;
            side.RowStyles.Add(new RowStyle(SizeType.Absolute, 128));
            side.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            side.RowStyles.Add(new RowStyle(SizeType.Absolute, 50));

            _connectionCard = CreateCard(0, 0, 0, 0);
            _connectionCard.Padding = new Padding(18, 14, 18, 14);
            side.Controls.Add(_connectionCard, 0, 0);

            TableLayoutPanel connection = new TableLayoutPanel();
            connection.Dock = DockStyle.Fill;
            connection.BackColor = _panelBackColor;
            connection.RowCount = 3;
            connection.ColumnCount = 2;
            connection.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 34));
            connection.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            connection.RowStyles.Add(new RowStyle(SizeType.Absolute, 30));
            connection.RowStyles.Add(new RowStyle(SizeType.Absolute, 32));
            connection.RowStyles.Add(new RowStyle(SizeType.Absolute, 28));
            _connectionCard.Controls.Add(connection);

            Label title = MakeSectionTitle("连接概览");
            connection.Controls.Add(title, 0, 0);
            connection.SetColumnSpan(title, 2);

            _connectionDotLabel = new Label();
            _connectionDotLabel.Dock = DockStyle.Fill;
            _connectionDotLabel.Text = "●";
            _connectionDotLabel.Font = new Font(Font.FontFamily, 16f, FontStyle.Bold);
            _connectionDotLabel.TextAlign = ContentAlignment.MiddleCenter;
            connection.Controls.Add(_connectionDotLabel, 0, 1);

            _clientLabel = new Label();
            _clientLabel.Dock = DockStyle.Fill;
            _clientLabel.Text = "客户端: -";
            _clientLabel.Font = new Font(Font.FontFamily, 11f, FontStyle.Bold);
            _clientLabel.ForeColor = _textColor;
            _clientLabel.TextAlign = ContentAlignment.MiddleLeft;
            connection.Controls.Add(_clientLabel, 1, 1);

            _lastUpdateLabel = MakeMutedLabel("最近更新: -");
            connection.Controls.Add(_lastUpdateLabel, 1, 2);

            _logTabs = new TabControl();
            _logTabs.Dock = DockStyle.Fill;
            _logTabs.Font = new Font(Font.FontFamily, 9f);
            _logTabs.Margin = new Padding(0, 14, 0, 0);
            side.Controls.Add(_logTabs, 0, 1);

            _liveLogText = AddLogTab(_logTabs, "事件");
            _alarmLogText = AddLogTab(_logTabs, "报警");
            _historyText = AddLogTab(_logTabs, "历史");

            _rawLogText = new TextBox();
            _rawLogText.Multiline = true;
            _rawLogText.ScrollBars = ScrollBars.Both;
            _rawLogText.ReadOnly = true;
            _rawLogText.Font = new Font("Consolas", 9f);
            _rawLogText.BorderStyle = BorderStyle.None;

            Button historyButton = MakeOutlineButton("查询报警历史");
            historyButton.Dock = DockStyle.Fill;
            historyButton.Margin = new Padding(0, 14, 0, 0);
            historyButton.Click += delegate { SendMessage(TcpMessageType.HistoryQuery, "{}"); };
            side.Controls.Add(historyButton, 0, 2);

            return side;
        }

        private TextBox AddLogTab(TabControl tabs, string title)
        {
            TabPage page = new TabPage(title);
            page.BackColor = _panelBackColor;
            page.Padding = new Padding(10);

            TextBox text = new TextBox();
            text.Dock = DockStyle.Fill;
            text.Multiline = true;
            text.ScrollBars = ScrollBars.Vertical;
            text.ReadOnly = true;
            text.Font = new Font("Consolas", 9.5f);
            text.BackColor = _panelBackColor;
            text.ForeColor = _textColor;
            text.BorderStyle = BorderStyle.None;
            page.Controls.Add(text);
            tabs.TabPages.Add(page);
            return text;
        }

        private NumericUpDown AddMetricNumeric(
            TableLayoutPanel parent,
            string labelText,
            string unitText,
            int min,
            int max,
            int value,
            int column,
            int row)
        {
            TableLayoutPanel panel = new TableLayoutPanel();
            panel.Dock = DockStyle.Fill;
            panel.BackColor = _panelBackColor;
            panel.Margin = new Padding(column == 0 ? 0 : 8, 0, column == 3 ? 0 : 8, 0);
            panel.RowCount = 2;
            panel.ColumnCount = 1;
            panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 24));
            panel.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            parent.Controls.Add(panel, column, row);

            Label label = MakeMutedLabel(labelText + " (" + unitText + ")");
            panel.Controls.Add(label, 0, 0);

            NumericUpDown input = new NumericUpDown();
            input.Minimum = min;
            input.Maximum = max;
            input.Value = value;
            input.Dock = DockStyle.Fill;
            input.Font = new Font(Font.FontFamily, 11f);
            input.BorderStyle = BorderStyle.FixedSingle;
            input.BackColor = Color.White;
            input.ForeColor = _textColor;
            panel.Controls.Add(input, 0, 1);
            return input;
        }

        private void AddActionButton(TableLayoutPanel parent, string text, string command, int column)
        {
            Button button = MakeOutlineButton(text);
            button.Click += delegate { SendCommand(command); };
            parent.Controls.Add(button, column, 0);
        }

        private CleanPanel CreateCard(int left, int top, int right, int bottom)
        {
            CleanPanel card = new CleanPanel(_borderColor);
            card.Dock = DockStyle.Fill;
            card.BackColor = _panelBackColor;
            card.Margin = new Padding(left, top, right, bottom);
            card.Padding = new Padding(18);
            return card;
        }

        private Label MakeSectionTitle(string text)
        {
            Label label = new Label();
            label.Text = text;
            label.Dock = DockStyle.Fill;
            label.Font = new Font(Font.FontFamily, 12f, FontStyle.Bold);
            label.ForeColor = _textColor;
            label.TextAlign = ContentAlignment.MiddleLeft;
            return label;
        }

        private Label MakeCardCaption(string text)
        {
            Label label = new Label();
            label.Text = text;
            label.Dock = DockStyle.Fill;
            label.Font = new Font(Font.FontFamily, 10f, FontStyle.Bold);
            label.ForeColor = _mutedTextColor;
            label.TextAlign = ContentAlignment.MiddleLeft;
            return label;
        }

        private Label MakeMutedLabel(string text)
        {
            Label label = new Label();
            label.Text = text;
            label.Dock = DockStyle.Fill;
            label.Font = new Font(Font.FontFamily, 9.5f);
            label.ForeColor = _mutedTextColor;
            label.TextAlign = ContentAlignment.MiddleLeft;
            label.AutoEllipsis = true;
            return label;
        }

        private Label MakeLabel(string text, int width)
        {
            Label label = new Label();
            label.Text = text;
            label.Width = width;
            label.Height = 32;
            label.Margin = new Padding(8, 0, 4, 0);
            label.ForeColor = _mutedTextColor;
            label.TextAlign = ContentAlignment.MiddleLeft;
            return label;
        }

        private TextBox MakeTextBox(string text, int width)
        {
            TextBox input = new TextBox();
            input.Width = width;
            input.Height = 28;
            input.Text = text;
            input.Font = new Font(Font.FontFamily, 10f);
            input.BorderStyle = BorderStyle.FixedSingle;
            input.Margin = new Padding(0, 2, 8, 0);
            return input;
        }

        private Button MakeButton(string text, Color backColor)
        {
            Button button = new Button();
            button.Text = text;
            button.Height = 32;
            button.FlatStyle = FlatStyle.Flat;
            button.FlatAppearance.BorderSize = 0;
            button.BackColor = backColor;
            button.ForeColor = Color.White;
            button.Font = new Font(Font.FontFamily, 9.5f, FontStyle.Bold);
            button.Margin = new Padding(4, 0, 0, 0);
            button.UseVisualStyleBackColor = false;
            return button;
        }

        private Button MakeOutlineButton(string text)
        {
            Button button = new Button();
            button.Text = text;
            button.Dock = DockStyle.Fill;
            button.Height = 38;
            button.FlatStyle = FlatStyle.Flat;
            button.FlatAppearance.BorderColor = _borderColor;
            button.FlatAppearance.BorderSize = 1;
            button.BackColor = Color.White;
            button.ForeColor = _textColor;
            button.Font = new Font(Font.FontFamily, 9.5f, FontStyle.Bold);
            button.Margin = new Padding(4, 8, 4, 8);
            button.UseVisualStyleBackColor = false;
            return button;
        }

        private void ConnectButton_Click(object sender, EventArgs e)
        {
            int port;
            string host = _hostText.Text.Trim();
            if (string.IsNullOrEmpty(host) || !int.TryParse(_portText.Text.Trim(), out port))
            {
                AppendRaw("Invalid host or port.");
                AppendLive("连接参数无效。");
                return;
            }

            StopCurrentConnection(true);
            _connectedHost = host;
            _connectedPort = port;
            _autoReconnectInProgress = false;
            _dataStale = false;
            _lastRecoveryProbeAt = DateTime.MinValue;
            UpdateConnectionUi(false);
            _connectionLabel.Text = "正在连接 " + host + ":" + port + "...";
            _closing = false;

            Thread worker = new Thread(new ThreadStart(delegate { ConnectWorker(host, port); }));
            worker.IsBackground = true;
            worker.Start();
        }

        private void ConnectWorker(string host, int port)
        {
            int generation;
            try
            {
                TcpClient client = new TcpClient();
                client.NoDelay = true;
                client.SendTimeout = 2000;
                client.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.KeepAlive, true);
                client.Connect(host, port);

                lock (_sendLock)
                {
                    _connectionGeneration++;
                    generation = _connectionGeneration;
                    _client = client;
                    _stream = client.GetStream();
                    _closing = false;
                }

                lock (_rxBuffer)
                {
                    _rxBuffer.Clear();
                }
                ClearPendingFrames();

                SafeInvoke(delegate
                {
                    _connectedHost = host;
                    _connectedPort = port;
                    _autoReconnectInProgress = false;
                    _loginReceived = false;
                    _loginAttemptCount = 0;
                    _dataStale = false;
                    _connectedAt = DateTime.Now;
                    _lastFrameAt = _connectedAt;
                    _lastDataFrameAt = _connectedAt;
                    _lastLoginSentAt = DateTime.MinValue;
                    _lastTxAt = DateTime.MinValue;
                    _lastRecoveryProbeAt = DateTime.MinValue;
                    UpdateConnectionUi(true);
                    AppendRaw("Connected to " + host + ":" + port);
                    AppendLive("已连接设备 " + host + ":" + port);
                });

                _rxThread = new Thread(new ThreadStart(delegate { ReceiveLoop(generation); }));
                _rxThread.IsBackground = true;
                _rxThread.Start();

                Thread.Sleep(200);
                SendLoginRequest();
            }
            catch (Exception ex)
            {
                SafeInvoke(delegate
                {
                    AppendRaw("Connect failed: " + ex.Message);
                    AppendLive("连接失败: " + ex.Message);
                    _autoReconnectInProgress = false;
                    UpdateConnectionUi(false);
                });
                DisconnectCore(false);
            }
        }

        private void ReceiveLoop(int generation)
        {
            byte[] temp = new byte[1024];

            while (!_closing)
            {
                try
                {
                    NetworkStream stream;
                    lock (_sendLock)
                    {
                        if (generation != _connectionGeneration)
                        {
                            break;
                        }

                        stream = _stream;
                    }

                    if (stream == null)
                    {
                        break;
                    }

                    int count = stream.Read(temp, 0, temp.Length);
                    if (count <= 0)
                    {
                        break;
                    }

                    List<TcpFrame> frames;
                    lock (_rxBuffer)
                    {
                        for (int i = 0; i < count; i++)
                        {
                            _rxBuffer.Add(temp[i]);
                        }

                        frames = TcpProtocol.ExtractFrames(_rxBuffer, AppendRaw);
                    }

                    if (frames.Count > 0)
                    {
                        MarkFramesReceived(frames);
                        EnqueueFrames(frames);
                    }
                }
                catch (Exception ex)
                {
                    if (!_closing)
                    {
                        SafeInvoke(delegate
                        {
                            AppendRaw("Receive error: " + ex.Message);
                            AppendLive("接收异常: " + ex.Message);
                        });
                    }
                    break;
                }
            }

            if (!_closing)
            {
                SafeInvoke(delegate
                {
                    if (generation == _connectionGeneration)
                    {
                        AppendRaw("Connection closed.");
                        AppendLive("连接已关闭。");
                        BeginAutoReconnect("连接关闭");
                    }
                });
            }
        }

        private void SendMessage(TcpMessageType type, string json)
        {
            try
            {
                NetworkStream stream;
                byte[] frame;
                lock (_sendLock)
                {
                    stream = _stream;
                    if (stream == null)
                    {
                        AppendRaw("Not connected.");
                        AppendLive("尚未连接设备。");
                        return;
                    }

                    frame = TcpProtocol.BuildFrame(type, NextSequence(), json);
                }

                lock (_writeLock)
                {
                    WaitForSendGapLocked();
                    stream.Write(frame, 0, frame.Length);
                    stream.Flush();
                }

                AppendRaw("TX " + TcpProtocol.GetTypeName(type) + " " + json);
            }
            catch (Exception ex)
            {
                AppendRaw("Send failed: " + ex.Message);
                AppendLive("发送失败: " + ex.Message);
                BeginAutoReconnect("发送失败");
            }
        }

        private void WaitForSendGapLocked()
        {
            DateTime now = DateTime.Now;
            if (_lastTxAt != DateTime.MinValue)
            {
                int waitMs = MinSendGapMilliseconds - (int)(now - _lastTxAt).TotalMilliseconds;
                if (waitMs > 0)
                {
                    Thread.Sleep(waitMs);
                    now = DateTime.Now;
                }
            }

            _lastTxAt = now;
        }

        private void SendLoginRequest()
        {
            _lastLoginSentAt = DateTime.Now;
            _loginAttemptCount++;
            AppendLive("发送登录请求。");
            SendMessage(TcpMessageType.LoginReq, "{\"client_type\":\"pc\",\"client_id\":\"PC_TcpUpper\",\"protocol\":1}");
        }

        private uint NextSequence()
        {
            uint value = _sequence;
            _sequence++;
            if (_sequence == 0)
            {
                _sequence = 1;
            }
            return value;
        }

        private void SendConfig_Click(object sender, EventArgs e)
        {
            Dictionary<string, object> config = new Dictionary<string, object>();
            config["alarm_hold_ms"] = (int)_alarmHoldInput.Value;
            config["fusion_window_ms"] = (int)_fusionInput.Value;
            config["audio_medium_ratio_pct"] = (int)_audioMediumInput.Value;
            config["audio_strong_ratio_pct"] = (int)_audioStrongInput.Value;
            SendMessage(TcpMessageType.ConfigSet, _json.Serialize(config));
        }

        private void SendCommand(string command)
        {
            Dictionary<string, object> body = new Dictionary<string, object>();
            body["cmd"] = command;
            SendMessage(TcpMessageType.ControlCmd, _json.Serialize(body));
        }

        private void HeartbeatTimer_Tick(object sender, EventArgs e)
        {
            if (_loginReceived)
            {
                SendMessage(TcpMessageType.Ping, "{}");
            }
        }

        private void StaleTimer_Tick(object sender, EventArgs e)
        {
            if (!HasActiveConnection())
            {
                return;
            }

            DateTime now = DateTime.Now;
            if (!_loginReceived)
            {
                if ((_lastLoginSentAt == DateTime.MinValue) ||
                    ((now - _lastLoginSentAt).TotalSeconds >= LoginRetrySeconds))
                {
                    SendLoginRequest();
                }

                if ((_connectedAt != DateTime.MinValue) &&
                    ((now - _connectedAt).TotalSeconds >= LoginReconnectSeconds))
                {
                    BeginAutoReconnect("登录无响应");
                }
                return;
            }

            if (_lastDataFrameAt == DateTime.MinValue)
            {
                _lastDataFrameAt = now;
                return;
            }

            double staleSeconds = (now - _lastDataFrameAt).TotalSeconds;
            if (staleSeconds < DataStaleProbeSeconds)
            {
                if (_dataStale)
                {
                    _dataStale = false;
                    _connectionLabel.Text = "已连接，正在接收实时数据";
                    _connectionDotLabel.ForeColor = _successColor;
                }
                return;
            }

            _dataStale = true;
            _connectionDotLabel.ForeColor = _warningColor;
            _connectionLabel.Text = "连接存在，但数据 " + ((int)staleSeconds).ToString() + " 秒未更新，正在恢复...";

            if ((now - _lastRecoveryProbeAt).TotalSeconds >= RecoveryProbeIntervalSeconds)
            {
                _lastRecoveryProbeAt = now;
                SendMessage(TcpMessageType.StatusQuery, "{}");
            }

            if (staleSeconds >= DataStaleReconnectSeconds)
            {
                BeginAutoReconnect("数据 " + ((int)staleSeconds).ToString() + " 秒未更新");
            }
        }

        private void FramePumpTimer_Tick(object sender, EventArgs e)
        {
            List<TcpFrame> frames = DequeueFramesForUi();
            if (frames.Count > 0)
            {
                HandleFrameBatch(frames);
            }
        }

        private void MarkFramesReceived(List<TcpFrame> frames)
        {
            DateTime now = DateTime.Now;
            _lastFrameAt = now;

            for (int i = 0; i < frames.Count; i++)
            {
                if (IsDataFrame(frames[i].Type))
                {
                    _lastDataFrameAt = now;
                    break;
                }
            }
        }

        private void EnqueueFrames(List<TcpFrame> frames)
        {
            lock (_frameQueueLock)
            {
                for (int i = 0; i < frames.Count; i++)
                {
                    TcpFrame frame = frames[i];
                    if (_pendingFrames.Count >= MaxQueuedFrames)
                    {
                        if (!DropOldestRealtimeFrameLocked() && IsRealtimeFrame(frame.Type))
                        {
                            continue;
                        }

                        while (_pendingFrames.Count >= MaxQueuedFrames)
                        {
                            _pendingFrames.Dequeue();
                        }
                    }

                    _pendingFrames.Enqueue(frame);
                }
            }
        }

        private List<TcpFrame> DequeueFramesForUi()
        {
            List<TcpFrame> frames = new List<TcpFrame>();

            lock (_frameQueueLock)
            {
                while ((_pendingFrames.Count > 0) && (frames.Count < MaxFramesPerUiPump))
                {
                    frames.Add(_pendingFrames.Dequeue());
                }
            }

            return frames;
        }

        private void ClearPendingFrames()
        {
            lock (_frameQueueLock)
            {
                _pendingFrames.Clear();
            }
        }

        private bool DropOldestRealtimeFrameLocked()
        {
            if (_pendingFrames.Count == 0)
            {
                return false;
            }

            Queue<TcpFrame> keptFrames = new Queue<TcpFrame>(_pendingFrames.Count);
            bool dropped = false;

            while (_pendingFrames.Count > 0)
            {
                TcpFrame queued = _pendingFrames.Dequeue();
                if (!dropped && IsRealtimeFrame(queued.Type))
                {
                    dropped = true;
                    continue;
                }

                keptFrames.Enqueue(queued);
            }

            while (keptFrames.Count > 0)
            {
                _pendingFrames.Enqueue(keptFrames.Dequeue());
            }

            return dropped;
        }

        private bool IsRealtimeFrame(TcpMessageType type)
        {
            return type == TcpMessageType.StatusPush ||
                   type == TcpMessageType.AudioReport ||
                   type == TcpMessageType.VibrationReport;
        }

        private bool IsDataFrame(TcpMessageType type)
        {
            return type == TcpMessageType.StatusRsp ||
                   type == TcpMessageType.StatusPush ||
                   type == TcpMessageType.ConfigRsp ||
                   type == TcpMessageType.ControlRsp ||
                   type == TcpMessageType.AudioReport ||
                   type == TcpMessageType.VibrationReport ||
                   type == TcpMessageType.AlarmReport ||
                   type == TcpMessageType.HistoryRsp;
        }

        private void HandleFrameBatch(List<TcpFrame> frames)
        {
            for (int i = 0; i < frames.Count; i++)
            {
                try
                {
                    HandleFrame(frames[i]);
                }
                catch (Exception ex)
                {
                    AppendRaw("Frame handling failed: " + ex.Message);
                }
            }
        }

        private void HandleFrame(TcpFrame frame)
        {
            string typeName = TcpProtocol.GetTypeName(frame.Type);
            if (ShouldLogRawFrame(frame.Type))
            {
                AppendRaw("RX " + typeName + " seq=" + frame.Sequence + " " + frame.Json);
            }
            MarkUpdated();

            Dictionary<string, object> body = ParseObject(frame.Json);

            switch (frame.Type)
            {
                case TcpMessageType.LoginRsp:
                    _loginReceived = true;
                    _loginAttemptCount = 0;
                    AppendLive("设备登录成功。");
                    UpdateLoginInfo(body);
                    break;

                case TcpMessageType.Pong:
                    AppendLive("心跳正常，tick=" + GetString(body, "tick", "-"));
                    break;

                case TcpMessageType.StatusRsp:
                case TcpMessageType.StatusPush:
                case TcpMessageType.ConfigRsp:
                case TcpMessageType.ControlRsp:
                    UpdateStatus(body);
                    MarkDataUpdated();
                    AppendLiveRealtime(GetFriendlyTypeName(frame.Type) + " 已更新。");
                    break;

                case TcpMessageType.AudioReport:
                    UpdateAudio(body);
                    MarkDataUpdated();
                    AppendLiveRealtime("声音上报: " + BuildAudioSummary(body));
                    break;

                case TcpMessageType.VibrationReport:
                    UpdateVibration(body);
                    MarkDataUpdated();
                    AppendLiveRealtime("振动上报: " + BuildVibrationSummary(body));
                    break;

                case TcpMessageType.AlarmReport:
                    UpdateAlarm(body);
                    MarkDataUpdated();
                    AppendAlarm("报警: " + BuildAlarmSummary(body));
                    break;

                case TcpMessageType.HistoryRsp:
                    UpdateHistory(body);
                    MarkDataUpdated();
                    break;

                case TcpMessageType.ErrorRsp:
                    AppendRaw("Device error: " + frame.Json);
                    AppendLive("设备错误: " + GetString(body, "code_text", GetString(body, "code", "-")));
                    break;

                default:
                    AppendRaw("Unhandled message type: " + frame.RawType);
                    AppendLive("未处理消息类型: " + frame.RawType);
                    break;
            }
        }

        private void UpdateLoginInfo(Dictionary<string, object> body)
        {
            string deviceId = GetString(body, "device_id", "-");
            string serverIp = GetString(body, "server_ip", _hostText.Text.Trim());
            string serverPort = GetString(body, "server_port", _portText.Text.Trim());

            _deviceLabel.Text = "设备: " + deviceId;
            _clientLabel.Text = "服务端: " + serverIp + ":" + serverPort;
        }

        private void UpdateStatus(Dictionary<string, object> body)
        {
            Dictionary<string, object> audio = GetObject(body, "audio");
            Dictionary<string, object> vibration = GetObject(body, "vibration");
            Dictionary<string, object> config = GetObject(body, "config");
            Dictionary<string, object> tcp = GetObject(body, "tcp");

            string deviceId = GetString(body, "device_id", "-");
            string tick = GetString(body, "tick", "-");
            string state = GetString(body, "state", "-");
            int stateCode = GetInt(body, "state_code", -1);
            string reason = GetString(body, "reason", "-");
            string lastAlarmTick = GetString(body, "last_alarm_tick", "-");
            string armed = FormatOnOff(GetString(body, "armed", "-"), "已布防", "未布防");
            string silenced = FormatOnOff(GetString(body, "silenced", "-"), "静音", "响铃");
            string clients = GetString(body, "clients", "-");

            _deviceLabel.Text = "设备: " + deviceId + "  tick=" + tick;
            _stateLabel.Text = FormatState(state, stateCode);
            _stateLabel.ForeColor = GetStateColor(stateCode, state);
            _armedLabel.Text = "布防: " + armed + "  蜂鸣器: " + silenced;
            _alarmReasonLabel.Text = "原因: " + reason;
            _tickLabel.Text = "最后报警 tick: " + lastAlarmTick;
            if (tcp != null)
            {
                _clientLabel.Text = "在线客户端: " + clients +
                                    "  link=" + GetString(tcp, "active_link", "-") +
                                    " mask=" + GetString(tcp, "link_mask", "-") +
                                    " tx=" + GetString(tcp, "last_tx_status", "-") +
                                    " fail=" + GetString(tcp, "tx_fail_count", "-") +
                                    " rxType=" + GetString(tcp, "last_rx_type", "-") +
                                    " txType=" + GetString(tcp, "last_tx_type", "-") +
                                    " ok=" + GetString(tcp, "tx_ok_count", "-") +
                                    " uart=" + GetString(tcp, "uart_err", "-") +
                                    " ovf=" + GetString(tcp, "rx_ovf", "-");
            }
            else
            {
                _clientLabel.Text = "在线客户端: " + clients;
            }

            if (IsAlarmState(stateCode, state, reason))
            {
                _alarmStatusLabel.Text = "报警";
                _alarmStatusLabel.ForeColor = _dangerColor;
            }
            else
            {
                _alarmStatusLabel.Text = "正常";
                _alarmStatusLabel.ForeColor = _successColor;
            }

            if (audio != null)
            {
                UpdateAudioFromStatus(audio);
            }

            if (vibration != null)
            {
                UpdateVibrationFromStatus(vibration);
            }

            if (config != null)
            {
                SetNumeric(_alarmHoldInput, GetInt(config, "alarm_hold_ms", (int)_alarmHoldInput.Value));
                SetNumeric(_fusionInput, GetInt(config, "fusion_window_ms", (int)_fusionInput.Value));
                SetNumeric(_audioMediumInput, GetInt(config, "audio_medium_ratio_pct", (int)_audioMediumInput.Value));
                SetNumeric(_audioStrongInput, GetInt(config, "audio_strong_ratio_pct", (int)_audioStrongInput.Value));
            }
        }

        private void UpdateAudio(Dictionary<string, object> body)
        {
            string score = GetString(body, "score", "-");
            string ratio = GetString(body, "ratio_pct", "-");
            string rms = GetString(body, "rms_mv", "-");

            if (score != "-")
            {
                _audioValueLabel.Text = score + "分";
            }
            else if (ratio != "-")
            {
                _audioValueLabel.Text = ratio + "%";
            }
            else if (rms != "-")
            {
                _audioValueLabel.Text = rms + " mV";
            }
            else
            {
                _audioValueLabel.Text = "-";
            }

            ApplyValueFont(_audioValueLabel);
            _audioMetaLabel.Text = FormatWithUnit(GetString(body, "freq_hz", "-"), "Hz");
            _audioEnergyLabel.Text = GetString(body, "energy", "-");
            ApplyValueFont(_audioMetaLabel);
            ApplyValueFont(_audioEnergyLabel);
            _audioValueLabel.ForeColor = GetLevelColor(GetInt(body, "level", 0));
        }

        private void UpdateAudioFromStatus(Dictionary<string, object> body)
        {
            string score = GetString(body, "score", "-");
            string ratio = GetString(body, "ratio_pct", "-");
            _audioValueLabel.Text = score == "-" ? (ratio == "-" ? "-" : ratio + "%") : score + "分";
            ApplyValueFont(_audioValueLabel);
            _audioMetaLabel.Text = FormatWithUnit(GetString(body, "freq_hz", "-"), "Hz");
            _audioEnergyLabel.Text = GetString(body, "energy", "-");
            ApplyValueFont(_audioMetaLabel);
            ApplyValueFont(_audioEnergyLabel);
            _audioValueLabel.ForeColor = _primaryColor;
        }

        private void UpdateVibration(Dictionary<string, object> body)
        {
            string score = GetString(body, "score", "-");
            string peak = GetString(body, "peak_mv", "-");
            _vibrationValueLabel.Text = score == "-" ? (peak == "-" ? "-" : peak + " mV") : score + "分";
            ApplyValueFont(_vibrationValueLabel);
            _vibrationMetaLabel.Text = FormatWithUnit(GetString(body, "freq_hz", "-"), "Hz");
            _vibrationEnergyLabel.Text = GetString(body, "energy", "-");
            ApplyValueFont(_vibrationMetaLabel);
            ApplyValueFont(_vibrationEnergyLabel);
            _vibrationValueLabel.ForeColor = GetLevelColor(GetInt(body, "level", 0));
        }

        private void UpdateVibrationFromStatus(Dictionary<string, object> body)
        {
            string score = GetString(body, "score", "-");
            string peak = GetString(body, "peak_mv", "-");
            _vibrationValueLabel.Text = score == "-" ? (peak == "-" ? "-" : peak + " mV") : score + "分";
            ApplyValueFont(_vibrationValueLabel);
            _vibrationMetaLabel.Text = FormatWithUnit(GetString(body, "freq_hz", "-"), "Hz");
            _vibrationEnergyLabel.Text = GetString(body, "energy", "-");
            ApplyValueFont(_vibrationMetaLabel);
            ApplyValueFont(_vibrationEnergyLabel);
            _vibrationValueLabel.ForeColor = _primaryColor;
        }

        private void UpdateAlarm(Dictionary<string, object> body)
        {
            Dictionary<string, object> audio = GetObject(body, "audio");
            Dictionary<string, object> vibration = GetObject(body, "vibration");
            string state = GetString(body, "state", "ALARM");
            int stateCode = GetInt(body, "state_code", 4);

            _alarmStatusLabel.Text = "报警";
            _alarmStatusLabel.ForeColor = _dangerColor;
            _stateLabel.Text = FormatState(state, stateCode);
            _stateLabel.ForeColor = _dangerColor;
            _alarmReasonLabel.Text = "原因: " + GetString(body, "reason", "-");
            _tickLabel.Text = "报警 tick: " + GetString(body, "tick", "-");

            if (audio != null)
            {
                UpdateAudioFromStatus(audio);
            }

            if (vibration != null)
            {
                UpdateVibrationFromStatus(vibration);
            }
        }

        private void UpdateHistory(Dictionary<string, object> body)
        {
            object items;
            StringBuilder sb = new StringBuilder();
            sb.AppendLine("报警记录数量: " + GetString(body, "count", "0"));
            sb.AppendLine();

            if (body != null && body.TryGetValue("items", out items))
            {
                object[] array = items as object[];
                if (array != null && array.Length > 0)
                {
                    for (int i = 0; i < array.Length; i++)
                    {
                        Dictionary<string, object> item = array[i] as Dictionary<string, object>;
                        if (item != null)
                        {
                            sb.AppendLine(string.Format(
                                "#{0}  tick={1}  原因={2}  状态={3}",
                                i + 1,
                                GetString(item, "t", "-"),
                                GetString(item, "r", "-"),
                                FormatStateCode(GetString(item, "s", "-"))));
                            sb.AppendLine(string.Format(
                                "    声音 {0} Hz / 能量 {1} / 比例 {2}%    振动 {3} Hz / 峰值 {4} mV / 能量 {5}",
                                GetString(item, "af", "-"),
                                GetString(item, "ae", "-"),
                                GetString(item, "ar", "-"),
                                GetString(item, "vf", "-"),
                                GetString(item, "vp", "-"),
                                GetString(item, "ve", "-")));
                            sb.AppendLine();
                        }
                    }
                }
                else
                {
                    sb.AppendLine("暂无历史报警。");
                }
            }
            else
            {
                sb.AppendLine("暂无历史报警。");
            }

            _historyText.Text = sb.ToString();
            AppendLive("历史报警已更新。");
        }

        private Dictionary<string, object> ParseObject(string json)
        {
            if (string.IsNullOrEmpty(json))
            {
                return new Dictionary<string, object>();
            }

            try
            {
                return _json.DeserializeObject(json) as Dictionary<string, object> ?? new Dictionary<string, object>();
            }
            catch (Exception ex)
            {
                AppendRaw("JSON parse failed: " + ex.Message);
                return new Dictionary<string, object>();
            }
        }

        private Dictionary<string, object> GetObject(Dictionary<string, object> body, string key)
        {
            object value;
            if (body != null && body.TryGetValue(key, out value))
            {
                return value as Dictionary<string, object>;
            }
            return null;
        }

        private string GetString(Dictionary<string, object> body, string key, string fallback)
        {
            object value;
            if (body == null || !body.TryGetValue(key, out value) || value == null)
            {
                return fallback;
            }
            return Convert.ToString(value);
        }

        private int GetInt(Dictionary<string, object> body, string key, int fallback)
        {
            object value;
            if (body == null || !body.TryGetValue(key, out value) || value == null)
            {
                return fallback;
            }

            try
            {
                return Convert.ToInt32(value);
            }
            catch
            {
                return fallback;
            }
        }

        private void SetNumeric(NumericUpDown input, int value)
        {
            if (value < input.Minimum)
            {
                value = (int)input.Minimum;
            }
            else if (value > input.Maximum)
            {
                value = (int)input.Maximum;
            }

            input.Value = value;
        }

        private string BuildAudioSummary(Dictionary<string, object> body)
        {
            return "等级 " + FormatLevel(GetString(body, "level", "-")) +
                   "，评分 " + GetString(body, "score", "-") +
                   "，频率 " + GetString(body, "freq_hz", "-") +
                   " Hz，比例 " + GetString(body, "ratio_pct", "-") + "%";
        }

        private string BuildVibrationSummary(Dictionary<string, object> body)
        {
            return "等级 " + FormatLevel(GetString(body, "level", "-")) +
                   "，评分 " + GetString(body, "score", "-") +
                   "，频率 " + GetString(body, "freq_hz", "-") +
                   " Hz，峰值 " + GetString(body, "peak_mv", "-") + " mV";
        }

        private string BuildAlarmSummary(Dictionary<string, object> body)
        {
            return "原因 " + GetString(body, "reason", "-") +
                   "，状态 " + FormatState(GetString(body, "state", "-"), GetInt(body, "state_code", -1)) +
                   "，tick=" + GetString(body, "tick", "-");
        }

        private string GetFriendlyTypeName(TcpMessageType type)
        {
            switch (type)
            {
                case TcpMessageType.StatusRsp: return "状态";
                case TcpMessageType.StatusPush: return "状态推送";
                case TcpMessageType.ConfigRsp: return "参数";
                case TcpMessageType.ControlRsp: return "控制";
                default: return TcpProtocol.GetTypeName(type);
            }
        }

        private string FormatState(string state, int stateCode)
        {
            if (stateCode >= 0)
            {
                string mapped = FormatStateCode(Convert.ToString(stateCode));
                if (mapped != "-")
                {
                    return mapped;
                }
            }

            if (string.IsNullOrEmpty(state) || state == "-")
            {
                return "-";
            }

            switch (state.ToUpperInvariant())
            {
                case "DISARMED": return "撤防";
                case "WARMUP": return "预热";
                case "ARMED": return "布防";
                case "SUSPICIOUS": return "可疑";
                case "ALARM": return "报警";
                default: return state;
            }
        }

        private string FormatStateCode(string code)
        {
            switch (code)
            {
                case "0": return "撤防";
                case "1": return "预热";
                case "2": return "布防";
                case "3": return "可疑";
                case "4": return "报警";
                default: return string.IsNullOrEmpty(code) ? "-" : code;
            }
        }

        private string FormatLevel(string level)
        {
            switch (level)
            {
                case "0": return "低";
                case "1": return "中";
                case "2": return "强";
                default: return string.IsNullOrEmpty(level) ? "-" : level;
            }
        }

        private string FormatOnOff(string value, string onText, string offText)
        {
            if (value == "1" || string.Equals(value, "true", StringComparison.OrdinalIgnoreCase))
            {
                return onText;
            }

            if (value == "0" || string.Equals(value, "false", StringComparison.OrdinalIgnoreCase))
            {
                return offText;
            }

            return value;
        }

        private string FormatWithUnit(string value, string unit)
        {
            if (string.IsNullOrEmpty(value) || value == "-")
            {
                return "-";
            }

            return value + " " + unit;
        }

        private Color GetStateColor(int stateCode, string state)
        {
            if (stateCode == 4 || string.Equals(state, "ALARM", StringComparison.OrdinalIgnoreCase))
            {
                return _dangerColor;
            }

            if (stateCode == 3 || string.Equals(state, "SUSPICIOUS", StringComparison.OrdinalIgnoreCase))
            {
                return _warningColor;
            }

            if (stateCode == 2 || string.Equals(state, "ARMED", StringComparison.OrdinalIgnoreCase))
            {
                return _successColor;
            }

            return _offlineColor;
        }

        private Color GetLevelColor(int level)
        {
            if (level >= 2)
            {
                return _dangerColor;
            }

            if (level == 1)
            {
                return _warningColor;
            }

            return _primaryColor;
        }

        private void ApplyValueFont(Label label)
        {
            string text = label.Text ?? "";
            float size = text.Length > 8 ? 16f : text.Length > 6 ? 18f : 20f;
            if (Math.Abs(label.Font.Size - size) > 0.1f)
            {
                label.Font = new Font(Font.FontFamily, size, FontStyle.Bold);
            }
        }

        private bool IsAlarmState(int stateCode, string state, string reason)
        {
            if (stateCode == 4 || string.Equals(state, "ALARM", StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }

            return !string.IsNullOrEmpty(reason) &&
                   reason != "-" &&
                   !string.Equals(reason, "NONE", StringComparison.OrdinalIgnoreCase);
        }

        private void MarkUpdated()
        {
            _lastFrameAt = DateTime.Now;
        }

        private void MarkDataUpdated()
        {
            _lastDataFrameAt = DateTime.Now;
            _lastRecoveryProbeAt = DateTime.MinValue;
            if (_dataStale)
            {
                _dataStale = false;
                _connectionLabel.Text = "已连接，正在接收实时数据";
                _connectionDotLabel.ForeColor = _successColor;
            }
            _lastUpdateLabel.Text = "最近数据: " + _lastDataFrameAt.ToString("HH:mm:ss");
        }

        private void AppendLive(string text)
        {
            if (InvokeRequired)
            {
                SafeInvoke(delegate { AppendLive(text); });
                return;
            }
            AppendText(_liveLogText, text);
        }

        private void AppendAlarm(string text)
        {
            if (InvokeRequired)
            {
                SafeInvoke(delegate { AppendAlarm(text); });
                return;
            }
            AppendText(_alarmLogText, text);
        }

        private void AppendRaw(string text)
        {
            if (InvokeRequired)
            {
                SafeInvoke(delegate { AppendRaw(text); });
                return;
            }
            AppendText(_rawLogText, text);
        }

        private bool ShouldLogRawFrame(TcpMessageType type)
        {
            if (type == TcpMessageType.StatusPush ||
                type == TcpMessageType.AudioReport ||
                type == TcpMessageType.VibrationReport)
            {
                DateTime now = DateTime.Now;
                if ((now - _lastRawRealtimeLog).TotalMilliseconds < 1000.0)
                {
                    return false;
                }
                _lastRawRealtimeLog = now;
            }

            return true;
        }

        private void AppendLiveRealtime(string text)
        {
            DateTime now = DateTime.Now;
            if ((now - _lastLiveRealtimeLog).TotalMilliseconds < 500.0)
            {
                return;
            }

            _lastLiveRealtimeLog = now;
            AppendLive(text);
        }

        private void AppendText(TextBox textBox, string text)
        {
            if (textBox == null)
            {
                return;
            }

            string line = DateTime.Now.ToString("HH:mm:ss") + "  " + text + Environment.NewLine;
            textBox.AppendText(line);
            if (textBox.TextLength > MaxLogTextLength)
            {
                textBox.Select(0, textBox.TextLength - MaxLogTextLength);
                textBox.SelectedText = string.Empty;
            }
        }

        private void UpdateConnectionUi(bool connected)
        {
            _connectButton.Enabled = !connected;
            _disconnectButton.Enabled = connected;
            _hostText.Enabled = !connected;
            _portText.Enabled = !connected;

            if (connected)
            {
                _connectionLabel.Text = "已连接，正在接收实时数据";
                _connectionDotLabel.ForeColor = _successColor;
                _connectionDotLabel.Text = "●";
                _heartbeatTimer.Start();
                _staleTimer.Start();
                _framePumpTimer.Start();
            }
            else
            {
                _connectionLabel.Text = "未连接设备";
                _connectionDotLabel.ForeColor = _offlineColor;
                _connectionDotLabel.Text = "●";
                _clientLabel.Text = "客户端: -";
                _lastUpdateLabel.Text = "最近更新: -";
                _heartbeatTimer.Stop();
                _staleTimer.Stop();
                _framePumpTimer.Stop();
                ClearPendingFrames();
                _dataStale = false;
                _loginReceived = false;
                _loginAttemptCount = 0;
                _lastLoginSentAt = DateTime.MinValue;
                _lastTxAt = DateTime.MinValue;
                _connectedAt = DateTime.MinValue;
            }
        }

        private void DisconnectCore(bool log)
        {
            DisconnectCore(log, false);
        }

        private void DisconnectCore(bool log, bool keepReconnectState)
        {
            _closing = true;
            if (!keepReconnectState)
            {
                _autoReconnectInProgress = false;
            }
            lock (_sendLock)
            {
                if (_client != null)
                {
                    try { _client.Client.Shutdown(SocketShutdown.Both); }
                    catch { }
                    try { _client.Client.LingerState = new LingerOption(false, 0); }
                    catch { }
                }

                if (_stream != null)
                {
                    try { _stream.Close(); }
                    catch { }
                    _stream = null;
                }

                if (_client != null)
                {
                    try { _client.Close(); }
                    catch { }
                    _client = null;
                }
            }

            if (InvokeRequired)
            {
                SafeInvoke(delegate { DisconnectCore(log, keepReconnectState); });
                return;
            }

            UpdateConnectionUi(false);
            ClearPendingFrames();
            _loginReceived = false;
            _loginAttemptCount = 0;
            _lastLoginSentAt = DateTime.MinValue;
            _lastTxAt = DateTime.MinValue;
            _connectedAt = DateTime.MinValue;
            if (log)
            {
                AppendRaw("Disconnected.");
                AppendLive("已断开连接。");
            }
        }

        private void StopCurrentConnection(bool log)
        {
            bool wasConnected = HasActiveConnection();
            DisconnectCore(log && wasConnected);
        }

        private bool HasActiveConnection()
        {
            lock (_sendLock)
            {
                return _stream != null && _client != null && !_closing;
            }
        }

        private void BeginAutoReconnect(string reason)
        {
            if (InvokeRequired)
            {
                SafeInvoke(delegate { BeginAutoReconnect(reason); });
                return;
            }

            if (_autoReconnectInProgress || string.IsNullOrEmpty(_connectedHost))
            {
                return;
            }

            _autoReconnectInProgress = true;
            AppendRaw("Auto reconnect: " + reason);
            AppendLive("数据停更，正在重连。");
            DisconnectCore(false, true);
            _autoReconnectInProgress = true;

            string host = _connectedHost;
            int port = _connectedPort;
            Thread worker = new Thread(new ThreadStart(delegate
            {
                Thread.Sleep(1200);
                ConnectWorker(host, port);
            }));
            worker.IsBackground = true;
            worker.Start();
        }

        private void SafeInvoke(Action action)
        {
            if (IsDisposed)
            {
                return;
            }

            try
            {
                if (InvokeRequired)
                {
                    BeginInvoke(action);
                }
                else
                {
                    action();
                }
            }
            catch
            {
            }
        }

        private sealed class CleanPanel : Panel
        {
            private readonly Color _borderColor;

            public CleanPanel(Color borderColor)
            {
                _borderColor = borderColor;
                DoubleBuffered = true;
            }

            protected override void OnPaint(PaintEventArgs e)
            {
                base.OnPaint(e);
                using (Pen pen = new Pen(_borderColor))
                {
                    Rectangle rect = ClientRectangle;
                    rect.Width -= 1;
                    rect.Height -= 1;
                    e.Graphics.DrawRectangle(pen, rect);
                }
            }
        }
    }
}
