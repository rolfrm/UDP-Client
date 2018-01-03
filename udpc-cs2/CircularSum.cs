namespace udpc_cs2
{
    public class CircularSum
    {
        public double Sum;
        readonly double[] buffer;
        int count;
        int front;
        public CircularSum(int count)
        {
            buffer = new double[count];
        }

        int getTruePosition(int offset)
        {
            offset += front;
            if (offset >= count)
                offset -= count;
            return offset;
        }

        public double First()
        {
            int pos = getTruePosition(1);
            return buffer[pos];
        }

        public double Last()
        {
            int pos = getTruePosition(0);
            return buffer[pos];
        }

        public void Add(double value)
        {
            if (count < buffer.Length)
            {
                buffer[count] = value;
                count += 1;
                front += 1;
                Sum += value;
            }
            else
            {
                front = front + 1;
                if (front >= count)
                    front = 0;
                Sum = Sum - buffer[front] + (buffer[front] = value);
            }
            
        }
        
    }
}