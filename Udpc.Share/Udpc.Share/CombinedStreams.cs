using System;
using System.IO;
using System.Linq;

namespace Udpc.Share
{
    public class CombinedStreams : Stream
    {
        readonly Stream[] streams;
        readonly long length;
        public CombinedStreams(params Stream[] streams)
        {
            this.streams = streams;
            length = streams.Sum(x => x.Length);
        }
        
        public override void Flush()
        {
            foreach(var stream in streams) stream.Flush();
        }

        public override int Read(byte[] buffer, int offset, int count)
        {
            var index = Position;
            int streamOffset = 0;
            foreach (var x in streams)
            {
                if (index > x.Length)
                    index -= x.Length;
                else
                    break;
                streamOffset += 1;
            }

            int readCount = 0;

            while (count > 0 & Position + readCount < Length)
            {
                var s = streams[streamOffset];

                if (s.Position != index)
                    s.Seek(index, SeekOrigin.Begin);
                int r = s.Read(buffer, offset, count);
                offset += r;
                count -= r;
                index += r;
                readCount += r;
                if (s.Position == s.Length)
                {
                    streamOffset += 1;
                    index -= s.Length;
                }
            }

            Position += readCount;

            return readCount;
        }

        public override long Seek(long offset, SeekOrigin origin)
        {
            switch (origin)
            {
                case SeekOrigin.Begin:
                    Position = offset;
                    break;
                case SeekOrigin.Current:
                    Position += offset;
                    break;
                case SeekOrigin.End:
                    Position = length - offset - 1;
                    break;
            }

            return Position;
        }

        public override void SetLength(long value)
        {
            throw new InvalidOperationException("Combined stream does not support SetLength.");
        }

        public override void Write(byte[] buffer, int offset, int count)
        {
            throw new InvalidOperationException("Combined stream does not support Writes.");
        }

        public override bool CanRead => true;
        public override bool CanSeek => true;
        public override bool CanWrite => false;
        public override long Length => length;

        public override long Position
        {
            get; set;
        }

        public override void Close()
        {
            base.Close();
            foreach (var x in streams)
                x.Close();
        }
    }
}