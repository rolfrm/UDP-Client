using System;
using System.IO;

namespace Udpc.Share
{
    public class EventWrappedStream : Stream
    {
        public Action OnClosed;
        readonly Stream innerStream;
        public EventWrappedStream(Stream innerStream)
        {
            this.innerStream = innerStream;
        }

        public override void Close()
        {
            base.Close();
            innerStream.Close();
            OnClosed?.Invoke();
        }

        public override void Flush()
        {
            innerStream.Flush();
        }

        public override int Read(byte[] buffer, int offset, int count)
        {
            return innerStream.Read(buffer, offset, count);
        }

        public override long Seek(long offset, SeekOrigin origin)
        {
            return innerStream.Seek(offset, origin);
        }

        public override void SetLength(long value)
        {
            innerStream.SetLength(value);
        }

        public override void Write(byte[] buffer, int offset, int count)
        {
            innerStream.Write(buffer, offset, count);
        }

        public override bool CanRead => innerStream.CanRead;
        public override bool CanSeek => innerStream.CanSeek;
        public override bool CanWrite => innerStream.CanWrite;
        public override long Length => innerStream.Length;
        public override long Position
        {
            get => innerStream.Position;
            set => innerStream.Position = value;
        }
    }
}