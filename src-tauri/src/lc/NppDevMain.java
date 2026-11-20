// NppDev's LeetCode runner for Java: runs a file that only has `class Solution` like LeetCode
// does. Each input line is one argument in LeetCode's format; argument types come from the
// method itself (int, long, double, boolean, char, String, arrays, List<...>, ListNode, TreeNode).
import java.io.*;
import java.lang.reflect.*;
import java.util.*;

public class NppDevMain {
    // ---- a tiny reader for LeetCode's values: numbers, "strings", true/false, null, [lists]
    static String s;
    static int i;

    static void ws() { while (i < s.length() && Character.isWhitespace(s.charAt(i))) i++; }

    static Object value() {
        ws();
        if (i >= s.length()) return null;
        char c = s.charAt(i);
        if (c == '[') {
            i++;
            List<Object> list = new ArrayList<>();
            ws();
            if (i < s.length() && s.charAt(i) == ']') { i++; return list; }
            while (true) {
                list.add(value());
                ws();
                if (i < s.length() && s.charAt(i) == ',') { i++; continue; }
                if (i < s.length() && s.charAt(i) == ']') i++;
                return list;
            }
        }
        if (c == '"') {
            StringBuilder b = new StringBuilder();
            i++;
            while (i < s.length() && s.charAt(i) != '"') {
                char ch = s.charAt(i++);
                if (ch == '\\' && i < s.length()) { char e = s.charAt(i++); ch = e == 'n' ? '\n' : e == 't' ? '\t' : e; }
                b.append(ch);
            }
            i++;
            return b.toString();
        }
        if (s.startsWith("true", i)) { i += 4; return Boolean.TRUE; }
        if (s.startsWith("false", i)) { i += 5; return Boolean.FALSE; }
        if (s.startsWith("null", i)) { i += 4; return null; }
        int b = i;
        while (i < s.length() && "+-.eE0123456789".indexOf(s.charAt(i)) >= 0) i++;
        String num = s.substring(b, i);
        return num.contains(".") || num.contains("e") || num.contains("E") ? (Object) Double.parseDouble(num) : (Object) Long.parseLong(num);
    }

    // ---- LeetCode value -> the parameter's Java type
    @SuppressWarnings("unchecked")
    static Object convert(Object v, Type t) {
        Class<?> c = raw(t);
        if (c == int.class || c == Integer.class) return v == null ? 0 : ((Number) v).intValue();
        if (c == long.class || c == Long.class) return v == null ? 0L : ((Number) v).longValue();
        if (c == double.class || c == Double.class) return ((Number) v).doubleValue();
        if (c == float.class || c == Float.class) return ((Number) v).floatValue();
        if (c == boolean.class || c == Boolean.class) return v;
        if (c == char.class || c == Character.class) return v instanceof String ? ((String) v).charAt(0) : (char) ((Number) v).intValue();
        if (c == String.class) return v == null ? null : v.toString();
        if (c.getSimpleName().equals("ListNode")) return makeList((List<Object>) v, c);
        if (c.getSimpleName().equals("TreeNode")) return makeTree((List<Object>) v, c);
        if (c.isArray()) {
            List<Object> l = (List<Object>) v;
            Object arr = Array.newInstance(c.getComponentType(), l.size());
            for (int k = 0; k < l.size(); k++) Array.set(arr, k, convert(l.get(k), c.getComponentType()));
            return arr;
        }
        if (Collection.class.isAssignableFrom(c)) {
            Type item = t instanceof ParameterizedType ? ((ParameterizedType) t).getActualTypeArguments()[0] : Object.class;
            List<Object> out = new ArrayList<>();
            for (Object x : (List<Object>) v) out.add(convert(x, item));
            return out;
        }
        return v;
    }

    static Class<?> raw(Type t) {
        if (t instanceof Class) return (Class<?>) t;
        if (t instanceof ParameterizedType) return (Class<?>) ((ParameterizedType) t).getRawType();
        return Object.class;
    }

    static Object makeList(List<Object> vals, Class<?> c) {
        try {
            Object head = null, cur = null;
            Field next = c.getDeclaredField("next");
            next.setAccessible(true);
            for (Object v : vals) {
                Object node = c.getDeclaredConstructor(int.class).newInstance(((Number) v).intValue());
                if (head == null) head = node; else next.set(cur, node);
                cur = node;
            }
            return head;
        } catch (ReflectiveOperationException e) { throw new RuntimeException(e); }
    }

    static Object makeTree(List<Object> vals, Class<?> c) {
        try {
            if (vals.isEmpty() || vals.get(0) == null) return null;
            Constructor<?> make = c.getDeclaredConstructor(int.class);
            Field left = c.getDeclaredField("left"), right = c.getDeclaredField("right");
            left.setAccessible(true);
            right.setAccessible(true);
            Object root = make.newInstance(((Number) vals.get(0)).intValue());
            ArrayDeque<Object> q = new ArrayDeque<>();
            q.add(root);
            int k = 1;
            while (!q.isEmpty() && k < vals.size()) {
                Object n = q.poll();
                if (k < vals.size() && vals.get(k) != null) { Object x = make.newInstance(((Number) vals.get(k)).intValue()); left.set(n, x); q.add(x); }
                k++;
                if (k < vals.size() && vals.get(k) != null) { Object x = make.newInstance(((Number) vals.get(k)).intValue()); right.set(n, x); q.add(x); }
                k++;
            }
            return root;
        } catch (ReflectiveOperationException e) { throw new RuntimeException(e); }
    }

    // ---- result -> LeetCode's text
    static void show(StringBuilder b, Object v) {
        try {
            if (v == null) { b.append("null"); return; }
            Class<?> c = v.getClass();
            if (v instanceof Double || v instanceof Float) { b.append(String.format(Locale.ROOT, "%.5f", ((Number) v).doubleValue())); return; }
            if (v instanceof Number || v instanceof Boolean) { b.append(v); return; }
            if (v instanceof Character || v instanceof String) { b.append('"').append(v).append('"'); return; }
            if (c.getSimpleName().equals("ListNode")) {
                Field val = c.getDeclaredField("val"), next = c.getDeclaredField("next");
                val.setAccessible(true);
                next.setAccessible(true);
                b.append('[');
                for (Object n = v; n != null; n = next.get(n)) { if (n != v) b.append(','); b.append(val.get(n)); }
                b.append(']');
                return;
            }
            if (c.getSimpleName().equals("TreeNode")) {
                Field val = c.getDeclaredField("val"), left = c.getDeclaredField("left"), right = c.getDeclaredField("right");
                val.setAccessible(true);
                left.setAccessible(true);
                right.setAccessible(true);
                List<String> out = new ArrayList<>();
                ArrayDeque<Object> q = new ArrayDeque<>();
                List<Object> level = new ArrayList<>();
                level.add(v);
                for (int k = 0; k < level.size(); k++) {
                    Object n = level.get(k);
                    if (n == null) { out.add("null"); continue; }
                    out.add(String.valueOf(val.get(n)));
                    level.add(left.get(n));
                    level.add(right.get(n));
                }
                while (!out.isEmpty() && out.get(out.size() - 1).equals("null")) out.remove(out.size() - 1);
                b.append('[').append(String.join(",", out)).append(']');
                return;
            }
            if (c.isArray()) {
                b.append('[');
                for (int k = 0; k < Array.getLength(v); k++) { if (k > 0) b.append(','); show(b, Array.get(v, k)); }
                b.append(']');
                return;
            }
            if (v instanceof Iterable) {
                b.append('[');
                boolean first = true;
                for (Object x : (Iterable<?>) v) { if (!first) b.append(','); first = false; show(b, x); }
                b.append(']');
                return;
            }
            b.append(v);
        } catch (ReflectiveOperationException e) { throw new RuntimeException(e); }
    }

    public static void main(String[] argv) throws Exception {
        String name = argv[0];
        Class<?> sol = Class.forName("Solution");
        Method m = null;
        for (Method x : sol.getDeclaredMethods()) if (x.getName().equals(name) && !x.isSynthetic()) { m = x; break; }
        if (m == null) { System.err.println("NppDev: class Solution has no method " + name + "."); System.exit(1); }
        m.setAccessible(true);
        Type[] types = m.getGenericParameterTypes();
        int n = types.length;
        List<String> lines = new ArrayList<>();
        BufferedReader r = new BufferedReader(new InputStreamReader(System.in, "UTF-8"));
        for (String l; (l = r.readLine()) != null; ) if (!l.trim().isEmpty()) lines.add(l);
        if (lines.size() < n) {
            System.err.println("NppDev: " + name + "() takes " + n + " argument(s), one per input line; the input has " + lines.size() + ".");
            System.exit(1);
        }
        Constructor<?> ctor = sol.getDeclaredConstructor();
        ctor.setAccessible(true);
        StringBuilder out = new StringBuilder();
        for (int at = 0; at + n <= lines.size(); at += Math.max(n, 1)) {
            Object[] args = new Object[n];
            for (int k = 0; k < n; k++) { s = lines.get(at + k); i = 0; args[k] = convert(value(), types[k]); }
            Object result;
            try { result = m.invoke(ctor.newInstance(), args); }
            catch (InvocationTargetException e) { throw (e.getCause() instanceof Exception) ? (Exception) e.getCause() : e; }
            show(out, m.getReturnType() == void.class && n > 0 ? args[0] : result);
            out.append('\n');
            if (n == 0) break;
        }
        System.out.print(out);
    }
}
