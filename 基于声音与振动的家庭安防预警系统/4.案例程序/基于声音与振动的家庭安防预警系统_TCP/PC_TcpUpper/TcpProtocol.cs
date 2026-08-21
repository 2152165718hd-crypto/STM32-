using System;
using System.Collections.Generic;
using System.Text;

namespace PC_TcpUpper
{
    internal enum TcpMessageType : ushort
    {
        LoginReq = 0x0001,
        LoginRsp = 0x0002,
        Ping = 0x0003,
        Pong = 0x0004,
        StatusQuery = 0x0010,
        StatusRsp = 0x0011,
        StatusPush = 0x0012,
        AudioReport = 0x0020,
        VibrationReport = 0x0021,
        AlarmReport = 0x0030,
        ConfigSet = 0x0040,
        ConfigRsp = 0x0041,
        ControlCmd = 0x0050,
        ControlRsp = 0x0051,
        HistoryQuery = 0x0060,
        HistoryRsp = 0x0061,
        ErrorRsp = 0x00FF
    }

    internal sealed class TcpFrame
    {
        public TcpMessageType Type { get; set; }
        public ushort RawType { get; set; }
        public uint Sequence { get; set; }
        public string Json { get; set; }
    }

    internal static class TcpProtocol
    {
        public const byte Version = 1;
        public const int HeaderSize = 16;
        public const int MaxBodySize = 4096;

        private const byte Magic0 = (byte)'H';
        private const byte Magic1 = (byte)'S';

        public static byte[] BuildFrame(TcpMessageType type, uint sequence, string json)
        {
            byte[] body = string.IsNullOrEmpty(json) ? new byte[0] : Encoding.UTF8.GetBytes(json);
            byte[] frame = new byte[HeaderSize + body.Length];

            frame[0] = Magic0;
            frame[1] = Magic1;
            frame[2] = Version;
            frame[3] = HeaderSize;
            WriteU16(frame, 4, (ushort)type);
            frame[6] = 0;
            frame[7] = 0;
            WriteU32(frame, 8, sequence);
            WriteU32(frame, 12, (uint)body.Length);
            Buffer.BlockCopy(body, 0, frame, HeaderSize, body.Length);
            return frame;
        }

        public static List<TcpFrame> ExtractFrames(List<byte> buffer, Action<string> logger)
        {
            List<TcpFrame> frames = new List<TcpFrame>();

            while (buffer.Count >= HeaderSize)
            {
                if (buffer[0] != Magic0 || buffer[1] != Magic1)
                {
                    int magicIndex = FindMagic(buffer);
                    int dropCount = magicIndex < 0 ? buffer.Count : magicIndex;

                    if (magicIndex < 0)
                    {
                        bool keepLast = buffer[buffer.Count - 1] == Magic0;
                        buffer.Clear();
                        if (keepLast)
                        {
                            buffer.Add(Magic0);
                        }
                    }
                    else
                    {
                        buffer.RemoveRange(0, magicIndex);
                    }

                    Log(logger, "RX desync: dropped non-frame bytes");
                    continue;
                }

                if (buffer[2] != Version || buffer[3] != HeaderSize)
                {
                    DropToNextMagicOrOneByte(buffer);
                    Log(logger, "RX desync: bad frame header");
                    continue;
                }

                ushort rawType = ReadU16(buffer, 4);
                uint sequence = ReadU32(buffer, 8);
                uint bodyLen = ReadU32(buffer, 12);

                if (bodyLen > MaxBodySize)
                {
                    int nextMagic = FindMagic(buffer, 1);
                    if (nextMagic > 0)
                    {
                        buffer.RemoveRange(0, nextMagic);
                    }
                    else
                    {
                        buffer.Clear();
                    }
                    Log(logger, "RX desync: oversize body_len=" + bodyLen);
                    continue;
                }

                int frameLen = HeaderSize + (int)bodyLen;
                if (buffer.Count < frameLen)
                {
                    int embeddedMagic = FindMagic(buffer, HeaderSize + 1);
                    if (embeddedMagic > 0)
                    {
                        buffer.RemoveRange(0, embeddedMagic);
                        Log(logger, "RX desync: incomplete frame skipped");
                        continue;
                    }
                    break;
                }

                if (bodyLen > 0 && (buffer[HeaderSize] != (byte)'{' || buffer[frameLen - 1] != (byte)'}'))
                {
                    DropToNextMagicOrOneByte(buffer);
                    Log(logger, "RX desync: body is truncated or mixed");
                    continue;
                }

                byte[] body = new byte[(int)bodyLen];
                if (bodyLen > 0)
                {
                    buffer.CopyTo(HeaderSize, body, 0, (int)bodyLen);
                }
                buffer.RemoveRange(0, frameLen);

                frames.Add(new TcpFrame
                {
                    RawType = rawType,
                    Type = (TcpMessageType)rawType,
                    Sequence = sequence,
                    Json = Encoding.UTF8.GetString(body)
                });
            }

            return frames;
        }

        public static string GetTypeName(TcpMessageType type)
        {
            switch (type)
            {
                case TcpMessageType.LoginReq: return "LOGIN_REQ";
                case TcpMessageType.LoginRsp: return "LOGIN_RSP";
                case TcpMessageType.Ping: return "PING";
                case TcpMessageType.Pong: return "PONG";
                case TcpMessageType.StatusQuery: return "STATUS_QUERY";
                case TcpMessageType.StatusRsp: return "STATUS_RSP";
                case TcpMessageType.StatusPush: return "STATUS_PUSH";
                case TcpMessageType.AudioReport: return "AUDIO_REPORT";
                case TcpMessageType.VibrationReport: return "VIBRATION_REPORT";
                case TcpMessageType.AlarmReport: return "ALARM_REPORT";
                case TcpMessageType.ConfigSet: return "CONFIG_SET";
                case TcpMessageType.ConfigRsp: return "CONFIG_RSP";
                case TcpMessageType.ControlCmd: return "CONTROL_CMD";
                case TcpMessageType.ControlRsp: return "CONTROL_RSP";
                case TcpMessageType.HistoryQuery: return "HISTORY_QUERY";
                case TcpMessageType.HistoryRsp: return "HISTORY_RSP";
                case TcpMessageType.ErrorRsp: return "ERROR_RSP";
                default: return "UNKNOWN";
            }
        }

        private static void DropToNextMagicOrOneByte(List<byte> buffer)
        {
            int nextMagic = FindMagic(buffer, 1);
            if (nextMagic > 0)
            {
                buffer.RemoveRange(0, nextMagic);
            }
            else if (buffer.Count > 0)
            {
                buffer.RemoveAt(0);
            }
        }

        private static int FindMagic(List<byte> buffer)
        {
            return FindMagic(buffer, 0);
        }

        private static int FindMagic(List<byte> buffer, int start)
        {
            if (start < 0)
            {
                start = 0;
            }

            for (int i = start; i + 1 < buffer.Count; i++)
            {
                if (buffer[i] == Magic0 && buffer[i + 1] == Magic1)
                {
                    return i;
                }
            }

            return -1;
        }

        private static string FormatBytes(List<byte> buffer, int offset, int count)
        {
            if (buffer == null || buffer.Count == 0 || count <= 0 || offset >= buffer.Count)
            {
                return "[]";
            }

            if (offset < 0)
            {
                offset = 0;
            }

            int end = Math.Min(buffer.Count, offset + Math.Min(count, 32));
            StringBuilder sb = new StringBuilder();
            sb.Append('[');
            for (int i = offset; i < end; i++)
            {
                if (i > offset)
                {
                    sb.Append(' ');
                }
                sb.Append(buffer[i].ToString("X2"));
            }
            if (offset + count > end)
            {
                sb.Append(" ...");
            }
            sb.Append(']');
            return sb.ToString();
        }

        private static void Log(Action<string> logger, string message)
        {
            if (logger != null)
            {
                logger(message);
            }
        }

        private static ushort ReadU16(List<byte> buffer, int offset)
        {
            return (ushort)((buffer[offset] << 8) | buffer[offset + 1]);
        }

        private static uint ReadU32(List<byte> buffer, int offset)
        {
            return ((uint)buffer[offset] << 24) |
                   ((uint)buffer[offset + 1] << 16) |
                   ((uint)buffer[offset + 2] << 8) |
                   buffer[offset + 3];
        }

        private static void WriteU16(byte[] data, int offset, ushort value)
        {
            data[offset] = (byte)((value >> 8) & 0xFF);
            data[offset + 1] = (byte)(value & 0xFF);
        }

        private static void WriteU32(byte[] data, int offset, uint value)
        {
            data[offset] = (byte)((value >> 24) & 0xFF);
            data[offset + 1] = (byte)((value >> 16) & 0xFF);
            data[offset + 2] = (byte)((value >> 8) & 0xFF);
            data[offset + 3] = (byte)(value & 0xFF);
        }
    }
}
