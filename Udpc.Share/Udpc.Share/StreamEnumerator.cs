using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;

namespace Udpc.Share
{
    public class StreamEnumerator<T> : IEnumerable<T>, IDisposable
    {
        readonly IEnumerable<T> data;
        Stream dataStream;

        public StreamEnumerator(Stream dataStream, IEnumerable<T> data)
        {
            this.data = data;
            this.dataStream = dataStream;
        }

        public IEnumerator<T> GetEnumerator()
        {
            if (dataStream == null) throw new ObjectDisposedException("StreamEnumerator");
            return data.GetEnumerator();
        }

        IEnumerator IEnumerable.GetEnumerator()
        {
            return GetEnumerator();
        }

        public void Dispose()
        {
            if (data == null) return;
            if (dataStream == null) throw new ObjectDisposedException("StreamEnumerator");
            dataStream.Dispose();
            dataStream = null;
        }

        ~StreamEnumerator()
        {
            if(dataStream != null)
                Dispose();
        }
    }
}