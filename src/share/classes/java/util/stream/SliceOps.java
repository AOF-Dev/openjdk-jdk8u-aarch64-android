/*
 * Copyright (c) 2012, 2013, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */
package java.util.stream;

import java.util.ArrayList;
import java.util.List;
import java.util.Spliterator;
import java.util.concurrent.CountedCompleter;
import java.util.function.IntFunction;

/**
 * Factory for instances of a short-circuiting stateful intermediate operations
 * that produce subsequences of their input stream.
 *
 * @since 1.8
 */
final class SliceOps {

    // No instances
    private SliceOps() { }

    /**
     * Appends a "slice" operation to the provided stream.  The slice operation
     * may be may be skip-only, limit-only, or skip-and-limit.
     *
     * @param <T> the type of both input and output elements
     * @param upstream a reference stream with element type T
     * @param skip the number of elements to skip.  Must be >= 0.
     * @param limit the maximum size of the resulting stream, or -1 if no limit
     *        is to be imposed
     */
    public static <T> Stream<T> makeRef(AbstractPipeline<?, T, ?> upstream,
                                       long skip, long limit) {
        if (skip < 0)
            throw new IllegalArgumentException("Skip must be non-negative: " + skip);

        return new ReferencePipeline.StatefulOp<T,T>(upstream, StreamShape.REFERENCE,
                                                     flags(limit)) {
            @Override
            <P_IN> Node<T> opEvaluateParallel(PipelineHelper<T> helper,
                                              Spliterator<P_IN> spliterator,
                                              IntFunction<T[]> generator) {
                return new SliceTask<>(this, helper, spliterator, generator, skip, limit).invoke();
            }

            @Override
            Sink<T> opWrapSink(int flags, Sink<T> sink) {
                return new Sink.ChainedReference<T>(sink) {
                    long n = skip;
                    long m = limit >= 0 ? limit : Long.MAX_VALUE;

                    @Override
                    public void accept(T t) {
                        if (n == 0) {
                            if (m > 0) {
                                m--;
                                downstream.accept(t);
                            }
                        }
                        else {
                            n--;
                        }
                    }

                    @Override
                    public boolean cancellationRequested() {
                        return m == 0 || downstream.cancellationRequested();
                    }
                };
            }
        };
    }

    /**
     * Appends a "slice" operation to the provided IntStream.  The slice
     * operation may be may be skip-only, limit-only, or skip-and-limit.
     *
     * @param upstream An IntStream
     * @param skip The number of elements to skip.  Must be >= 0.
     * @param limit The maximum size of the resulting stream, or -1 if no limit
     *        is to be imposed
     */
    public static IntStream makeInt(AbstractPipeline<?, Integer, ?> upstream,
                                    long skip, long limit) {
        if (skip < 0)
            throw new IllegalArgumentException("Skip must be non-negative: " + skip);

        return new IntPipeline.StatefulOp<Integer>(upstream, StreamShape.INT_VALUE,
                                                   flags(limit)) {
            @Override
            <P_IN> Node<Integer> opEvaluateParallel(PipelineHelper<Integer> helper,
                                                    Spliterator<P_IN> spliterator,
                                                    IntFunction<Integer[]> generator) {
                return new SliceTask<>(this, helper, spliterator, generator, skip, limit).invoke();
            }

            @Override
            Sink<Integer> opWrapSink(int flags, Sink<Integer> sink) {
                return new Sink.ChainedInt(sink) {
                    long n = skip;
                    long m = limit >= 0 ? limit : Long.MAX_VALUE;

                    @Override
                    public void accept(int t) {
                        if (n == 0) {
                            if (m > 0) {
                                m--;
                                downstream.accept(t);
                            }
                        }
                        else {
                            n--;
                        }
                    }

                    @Override
                    public boolean cancellationRequested() {
                        return m == 0 || downstream.cancellationRequested();
                    }
                };
            }
        };
    }

    /**
     * Appends a "slice" operation to the provided LongStream.  The slice
     * operation may be may be skip-only, limit-only, or skip-and-limit.
     *
     * @param upstream A LongStream
     * @param skip The number of elements to skip.  Must be >= 0.
     * @param limit The maximum size of the resulting stream, or -1 if no limit
     *        is to be imposed
     */
    public static LongStream makeLong(AbstractPipeline<?, Long, ?> upstream,
                                      long skip, long limit) {
        if (skip < 0)
            throw new IllegalArgumentException("Skip must be non-negative: " + skip);

        return new LongPipeline.StatefulOp<Long>(upstream, StreamShape.LONG_VALUE,
                                                 flags(limit)) {
            @Override
            <P_IN> Node<Long> opEvaluateParallel(PipelineHelper<Long> helper,
                                                 Spliterator<P_IN> spliterator,
                                                 IntFunction<Long[]> generator) {
                return new SliceTask<>(this, helper, spliterator, generator, skip, limit).invoke();
            }

            @Override
            Sink<Long> opWrapSink(int flags, Sink<Long> sink) {
                return new Sink.ChainedLong(sink) {
                    long n = skip;
                    long m = limit >= 0 ? limit : Long.MAX_VALUE;

                    @Override
                    public void accept(long t) {
                        if (n == 0) {
                            if (m > 0) {
                                m--;
                                downstream.accept(t);
                            }
                        }
                        else {
                            n--;
                        }
                    }

                    @Override
                    public boolean cancellationRequested() {
                        return m == 0 || downstream.cancellationRequested();
                    }
                };
            }
        };
    }

    /**
     * Appends a "slice" operation to the provided DoubleStream.  The slice
     * operation may be may be skip-only, limit-only, or skip-and-limit.
     *
     * @param upstream A DoubleStream
     * @param skip The number of elements to skip.  Must be >= 0.
     * @param limit The maximum size of the resulting stream, or -1 if no limit
     *        is to be imposed
     */
    public static DoubleStream makeDouble(AbstractPipeline<?, Double, ?> upstream,
                                          long skip, long limit) {
        if (skip < 0)
            throw new IllegalArgumentException("Skip must be non-negative: " + skip);

        return new DoublePipeline.StatefulOp<Double>(upstream, StreamShape.DOUBLE_VALUE,
                                                     flags(limit)) {
            @Override
            <P_IN> Node<Double> opEvaluateParallel(PipelineHelper<Double> helper,
                                                   Spliterator<P_IN> spliterator,
                                                   IntFunction<Double[]> generator) {
                return new SliceTask<>(this, helper, spliterator, generator, skip, limit).invoke();
            }

            @Override
            Sink<Double> opWrapSink(int flags, Sink<Double> sink) {
                return new Sink.ChainedDouble(sink) {
                    long n = skip;
                    long m = limit >= 0 ? limit : Long.MAX_VALUE;

                    @Override
                    public void accept(double t) {
                        if (n == 0) {
                            if (m > 0) {
                                m--;
                                downstream.accept(t);
                            }
                        }
                        else {
                            n--;
                        }
                    }

                    @Override
                    public boolean cancellationRequested() {
                        return m == 0 || downstream.cancellationRequested();
                    }
                };
            }
        };
    }

    private static int flags(long limit) {
        return StreamOpFlag.NOT_SIZED | ((limit != -1) ? StreamOpFlag.IS_SHORT_CIRCUIT : 0);
    }

    // Parallel strategy -- two cases
    // IF we have full size information
    // - decompose, keeping track of each leaf's (offset, size)
    // - calculate leaf only if intersection between (offset, size) and desired slice
    // - Construct a Node containing the appropriate sections of the appropriate leaves
    // IF we don't
    // - decompose, and calculate size of each leaf
    // - on complete of any node, compute completed initial size from the root, and if big enough, cancel later nodes
    // - @@@ this can be significantly improved

    // @@@ Currently we don't do the sized version at all

    // @@@ Should take into account ORDERED flag; if not ORDERED, we can limit in temporal order instead

    /**
     * {@code ForkJoinTask} implementing slice computation.
     *
     * @param <P_IN> Input element type to the stream pipeline
     * @param <P_OUT> Output element type from the stream pipeline
     */
    private static final class SliceTask<P_IN, P_OUT>
            extends AbstractShortCircuitTask<P_IN, P_OUT, Node<P_OUT>, SliceTask<P_IN, P_OUT>> {
        private final AbstractPipeline<P_OUT, P_OUT, ?> op;
        private final IntFunction<P_OUT[]> generator;
        private final long targetOffset, targetSize;
        private long thisNodeSize;

        private volatile boolean completed;

        SliceTask(AbstractPipeline<?, P_OUT, ?> op,
                  PipelineHelper<P_OUT> helper,
                  Spliterator<P_IN> spliterator,
                  IntFunction<P_OUT[]> generator,
                  long offset, long size) {
            super(helper, spliterator);
            this.op = (AbstractPipeline<P_OUT, P_OUT, ?>) op;
            this.generator = generator;
            this.targetOffset = offset;
            this.targetSize = size;
        }

        SliceTask(SliceTask<P_IN, P_OUT> parent, Spliterator<P_IN> spliterator) {
            super(parent, spliterator);
            this.op = parent.op;
            this.generator = parent.generator;
            this.targetOffset = parent.targetOffset;
            this.targetSize = parent.targetSize;
        }

        @Override
        protected SliceTask<P_IN, P_OUT> makeChild(Spliterator<P_IN> spliterator) {
            return new SliceTask<>(this, spliterator);
        }

        @Override
        protected final Node<P_OUT> getEmptyResult() {
            return Nodes.emptyNode(op.getOutputShape());
        }

        @Override
        protected final Node<P_OUT> doLeaf() {
            if (isRoot()) {
                long sizeIfKnown = StreamOpFlag.SIZED.isPreserved(op.sourceOrOpFlags)
                                   ? op.exactOutputSizeIfKnown(spliterator)
                                   : -1;
                final Node.Builder<P_OUT> nb = op.makeNodeBuilder(sizeIfKnown, generator);
                Sink<P_OUT> opSink = op.opWrapSink(op.sourceOrOpFlags, nb);

                if (!StreamOpFlag.SHORT_CIRCUIT.isKnown(op.sourceOrOpFlags))
                    helper.wrapAndCopyInto(opSink, spliterator);
                else
                    helper.copyIntoWithCancel(helper.wrapSink(opSink), spliterator);
                return nb.build();
            }
            else {
                Node<P_OUT> node = helper.wrapAndCopyInto(helper.makeNodeBuilder(-1, generator),
                                                      spliterator).build();
                thisNodeSize = node.count();
                completed = true;
                return node;
            }
        }

        @Override
        public final void onCompletion(CountedCompleter<?> caller) {
            if (!isLeaf()) {
                thisNodeSize = leftChild.thisNodeSize + rightChild.thisNodeSize;
                completed = true;

                if (isRoot()) {
                    // Only collect nodes once absolute size information is known

                    ArrayList<Node<P_OUT>> nodes = new ArrayList<>();
                    visit(nodes, 0);
                    Node<P_OUT> result;
                    if (nodes.size() == 0)
                        result = Nodes.emptyNode(op.getOutputShape());
                    else if (nodes.size() == 1)
                        result = nodes.get(0);
                    else
                        // This will create a tree of depth 1 and will not be a sub-tree
                        // for leaf nodes within the require range
                        result = Nodes.conc(op.getOutputShape(), nodes);
                    setLocalResult(result);
                }
            }
            if (targetSize >= 0) {
                if (((SliceTask<P_IN, P_OUT>) getRoot()).leftSize() >= targetOffset + targetSize)
                    cancelLaterNodes();
            }
            // Don't call super.onCompletion(), we don't look at the child nodes until farther up the tree
        }

        /** Compute the cumulative size of the longest leading prefix of completed children */
        private long leftSize() {
            if (completed)
                return thisNodeSize;
            else if (isLeaf())
                return 0;
            else {
                long leftSize = 0;
                for (SliceTask<P_IN, P_OUT> child = leftChild, p = null; child != p;
                     p = child, child = rightChild) {
                    if (child.completed)
                        leftSize += child.thisNodeSize;
                    else {
                        leftSize += child.leftSize();
                        break;
                    }
                }
                return leftSize;
            }
        }

        private void visit(List<Node<P_OUT>> results, int offset) {
            if (!isLeaf()) {
                for (SliceTask<P_IN, P_OUT> child = leftChild, p = null; child != p;
                     p = child, child = rightChild) {
                    child.visit(results, offset);
                    offset += child.thisNodeSize;
                }
            }
            else {
                if (results.size() == 0) {
                    if (offset + thisNodeSize >= targetOffset)
                        results.add(truncateNode(getLocalResult(),
                                                 Math.max(0, targetOffset - offset),
                                                 targetSize >= 0 ? Math.max(0, offset + thisNodeSize - (targetOffset + targetSize)) : 0));
                }
                else {
                    if (targetSize == -1 || offset < targetOffset + targetSize) {
                        results.add(truncateNode(getLocalResult(),
                                                 0,
                                                 targetSize >= 0 ? Math.max(0, offset + thisNodeSize - (targetOffset + targetSize)) : 0));
                    }
                }
            }
        }

        /**
         * Return a new node describing the result of truncating an existing Node
         * at the left and/or right.
         */
        private Node<P_OUT> truncateNode(Node<P_OUT> input,
                                         long skipLeft, long skipRight) {
            if (skipLeft == 0 && skipRight == 0)
                return input;
            else {
                return Nodes.truncateNode(input, skipLeft, thisNodeSize - skipRight, generator);
            }
        }
    }

    // @@@ Currently unused -- optimization for when all sizes are known
//    private static class SizedSliceTask<S, T> extends AbstractShortCircuitTask<S, T, Node<T>, SizedSliceTask<S, T>> {
//        private final int targetOffset, targetSize;
//        private final int offset, size;
//
//        private SizedSliceTask(ParallelPipelineHelper<S, T> helper, int offset, int size) {
//            super(helper);
//            targetOffset = offset;
//            targetSize = size;
//            this.offset = 0;
//            this.size = spliterator.getSizeIfKnown();
//        }
//
//        private SizedSliceTask(SizedSliceTask<S, T> parent, Spliterator<S> spliterator) {
//            // Makes assumptions about order in which siblings are created and linked into parent!
//            super(parent, spliterator);
//            targetOffset = parent.targetOffset;
//            targetSize = parent.targetSize;
//            int siblingSizes = 0;
//            for (SizedSliceTask<S, T> sibling = parent.children; sibling != null; sibling = sibling.nextSibling)
//                siblingSizes += sibling.size;
//            size = spliterator.getSizeIfKnown();
//            offset = parent.offset + siblingSizes;
//        }
//
//        @Override
//        protected SizedSliceTask<S, T> makeChild(Spliterator<S> spliterator) {
//            return new SizedSliceTask<>(this, spliterator);
//        }
//
//        @Override
//        protected Node<T> getEmptyResult() {
//            return Nodes.emptyNode();
//        }
//
//        @Override
//        public boolean taskCanceled() {
//            if (offset > targetOffset+targetSize || offset+size < targetOffset)
//                return true;
//            else
//                return super.taskCanceled();
//        }
//
//        @Override
//        protected Node<T> doLeaf() {
//            int skipLeft = Math.max(0, targetOffset - offset);
//            int skipRight = Math.max(0, offset + size - (targetOffset + targetSize));
//            if (skipLeft == 0 && skipRight == 0)
//                return helper.into(Nodes.<T>makeBuilder(spliterator.getSizeIfKnown())).build();
//            else {
//                // If we're the first or last node that intersects the target range, peel off irrelevant elements
//                int truncatedSize = size - skipLeft - skipRight;
//                NodeBuilder<T> builder = Nodes.<T>makeBuilder(truncatedSize);
//                Sink<S> wrappedSink = helper.wrapSink(builder);
//                wrappedSink.begin(truncatedSize);
//                Iterator<S> iterator = spliterator.iterator();
//                for (int i=0; i<skipLeft; i++)
//                    iterator.next();
//                for (int i=0; i<truncatedSize; i++)
//                    wrappedSink.apply(iterator.next());
//                wrappedSink.end();
//                return builder.build();
//            }
//        }
//
//        @Override
//        public void onCompletion(CountedCompleter<?> caller) {
//            if (!isLeaf()) {
//                Node<T> result = null;
//                for (SizedSliceTask<S, T> child = children.nextSibling; child != null; child = child.nextSibling) {
//                    Node<T> childResult = child.getRawResult();
//                    if (childResult == null)
//                        continue;
//                    else if (result == null)
//                        result = childResult;
//                    else
//                        result = Nodes.node(result, childResult);
//                }
//                setRawResult(result);
//                if (offset <= targetOffset && offset+size >= targetOffset+targetSize)
//                    shortCircuit(result);
//            }
//        }
//    }

}
