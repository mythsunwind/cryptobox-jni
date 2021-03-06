// Copyright (C) 2015 Wire Swiss GmbH <support@wire.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

package com.wire.cryptobox;

import java.util.HashMap;

/**
 * A <tt>CryptoBox</tt> is an opaque container of all the necessary key material
 * needed for exchanging end-to-end encrypted messages with peers for a single,
 * logical client or device. It maintains a pool of {@link CryptoSession}s for
 * all remote peers.
 *
 * <p>Every cryptographic session with a peer is represented by a {@link CryptoSession}.
 * These sessions are pooled by a <tt>CryptoBox</tt>, i.e. if a session with the
 * same session ID is requested multiple times, the same instance is returned.
 * Consequently, <tt>CryptoSession</tt>s are kept in memory once loaded. They
 * can be explicitly closed through {@link CryptoBox#closeSession} or
 * {@link CryptoBox#closeAllSessions}. All loaded sessions are implicitly closed
 * when the <tt>CryptoBox</tt> itself is closed via {@link CryptoBox#close}.
 * Note that it is considered programmer error to let a <tt>CryptoBox</tt>
 * become unreachable and thus eligible for garbage collection without having
 * called {@link CryptoBox#close}, even though this class overrides {@link Object#finalize}
 * as an additional safety net for deallocating all native resources.
 * </p>
 *
 * <p>A <tt>CryptoBox</tt> is thread-safe.</p>
 *
 * @see CryptoSession
 */
final public class CryptoBox {
    static {
        System.loadLibrary("sodium");
        System.loadLibrary("cryptobox");
        System.loadLibrary("cryptobox-jni");
    }

    /** The max ID of an ephemeral prekey generated by {@link #newPreKeys}. */
    public static final int MAX_PREKEY_ID  = 0xFFFE;

    /** The desired local storage mode for use with {@link #openWith}. */
    public enum IdentityMode { COMPLETE, PUBLIC }

    private static final Object OPEN_LOCK = new Object();
    private long ptr;
    private final Object lock = new Object();
    private final HashMap<String, CryptoSession> sessions = new HashMap<String, CryptoSession>();

    private CryptoBox(long ptr) {
        this.ptr = ptr;
    }

    /**
     * Open a <tt>CryptoBox</tt> that operates on the given directory.
     *
     * The given directory must exist and be writeable.
     *
     * <p>Note: Do not open multiple boxes that operate on the same or
     * overlapping directories. Doing so results in undefined behaviour.</p>
     *
     * @param dir The root storage directory of the box.
     */
    public static CryptoBox open(String dir) throws CryptoException {
        synchronized (OPEN_LOCK) {
            return jniOpen(dir);
        }
    }

    /**
     * Open a <tt>CryptoBox</tt> that operates on the given directory, using
     * an existing external identity.
     *
     * The given identity must match the (public or complete) identity that
     * the <tt>CryptoBox</tt> already has, if any.
     *
     * The given directory must exist and be writeable.
     *
     * <p>Note: Do not open multiple boxes that operate on the same or
     * overlapping directories. Doing so results in undefined behaviour.</p>
     *
     * @param dir The root storage directory of the box.
     * @param id The serialised external identity to use.
     * @param mode The desired local identity storage.
     */
    public static CryptoBox openWith(String dir, byte[] id, IdentityMode mode) throws CryptoException {
        synchronized (OPEN_LOCK) {
            switch (mode) {
                case COMPLETE: return jniOpenWith(dir, id, 0);
                case PUBLIC:   return jniOpenWith(dir, id, 1);
                default:       throw new IllegalStateException("Unexpected IdentityMode");
            }
        }
    }

    /**
     * Copy the long-term identity from this <tt>CryptoBox</tt>.
     *
     * @return The opaque, serialised identity to be stored in a safe place or
     *         transmitted over a safe channel for subsequent use with
     *         {@link CryptoBox#openWith}.
     */
    public byte[] copyIdentity() throws CryptoException {
        synchronized (lock) {
            errorIfClosed();
            return jniCopyIdentity(this.ptr);
        }
    }

    /**
     * Get the local fingerprint as a hex-encoded byte array.
     */
    public byte[] getLocalFingerprint() throws CryptoException {
        synchronized (lock) {
            errorIfClosed();
            return jniGetLocalFingerprint(this.ptr);
        }
    }

    /**
     * Generate a new last prekey.
     *
     * The last prekey is never removed as a result of {@link #initSessionFromMessage}.
     */
    public PreKey newLastPreKey() throws CryptoException {
        synchronized (lock) {
            errorIfClosed();
            return jniNewLastPreKey(this.ptr);
        }
    }

    /**
     * Generate a new batch of ephemeral prekeys.
     *
     * If <tt>start + num > {@link #MAX_PREKEY_ID}<tt/> the IDs wrap around and start
     * over at 0. Thus after any valid invocation of this method, the last generated
     * prekey ID is always <tt>(start + num) % ({@link #MAX_PREKEY_ID} + 1)</tt>. The caller
     * can remember that ID and feed it back into {@link #newPreKeys} as the start
     * ID when the next batch of ephemeral keys needs to be generated.
     *
     * @param start The ID (>= 0 and <= {@link #MAX_PREKEY_ID}) of the first prekey to generate.
     * @param num The total number of prekeys to generate (> 0 and <= {@link #MAX_PREKEY_ID}).
     */
    public PreKey[] newPreKeys(int start, int num) throws CryptoException {
        if (start < 0 || start > MAX_PREKEY_ID) {
            throw new IllegalArgumentException("start must be >= 0 and <= " + MAX_PREKEY_ID);
        }
        if (num < 1 || num > MAX_PREKEY_ID) {
            throw new IllegalArgumentException("num must be >= 1 and <= " + MAX_PREKEY_ID);
        }
        synchronized (lock) {
            errorIfClosed();
            return jniNewPreKeys(this.ptr, start, num);
        }
    }

    /**
     * Initialise a {@link CryptoSession} using the prekey of a peer.
     *
     * <p>This is the entry point for the initiator of a session, i.e.
     * the side that wishes to send the first message.</p>
     *
     * @param sid The ID of the new session.
     * @param prekey The prekey of the peer.
     */
    public CryptoSession initSessionFromPreKey(String sid, PreKey prekey) throws CryptoException {
        synchronized (lock) {
            errorIfClosed();
            CryptoSession sess = sessions.get(sid);
            if (sess != null) {
                return sess;
            }
            sess = jniInitSessionFromPreKey(this.ptr, sid, prekey.data);
            sessions.put(sess.id, sess);
            return sess;
        }
    }

    /**
     * Initialise a {@link CryptoSession} using a received encrypted message.
     *
     * <p>This is the entry point for the recipient of an encrypted message.</p>
     *
     * @param sid The ID of the new session.
     * @param message The encrypted (prekey) message.
     */
    public SessionMessage initSessionFromMessage(String sid, byte[] message) throws CryptoException {
        synchronized (lock) {
            errorIfClosed();
            CryptoSession sess = sessions.get(sid);
            if (sess != null) {
                return new SessionMessage(sess, sess.decrypt(message));
            }
            SessionMessage smsg = jniInitSessionFromMessage(this.ptr, sid, message);
            sessions.put(smsg.getSession().id, smsg.getSession());
            return smsg;
        }
    }

    /**
     * Get an existing session by ID.
     *
     * <p>If the session does not exist, a {@link CryptoException} is thrown
     * with the code {@link CryptoException.Code#SESSION_NOT_FOUND}.</p>
     *
     * @param sid The ID of the session to get.
     */
    public CryptoSession getSession(String sid) throws CryptoException {
        synchronized (lock) {
            errorIfClosed();
            CryptoSession sess = sessions.get(sid);
            if (sess == null) {
                sess = jniLoadSession(this.ptr, sid);
                sessions.put(sid, sess);
            }
            return sess;
        }
    }

    /**
     * Try to get an existing session by ID.
     *
     * <p>Equivalent to {@link #getSession}, except that <tt>null</tt> is
     * returned if the session does not exist.</p>
     *
     * @param sid The ID of the session to get.
     */
    public CryptoSession tryGetSession(String sid) throws CryptoException {
        try { return getSession(sid); }
        catch (CryptoException ex) {
            if (ex.code == CryptoException.Code.SESSION_NOT_FOUND) {
                return null;
            }
            throw ex;
        }
    }

    /**
     * Close a session.
     *
     * <p>Note: After a session has been closed, any operations other than
     * <tt>closeSession</tt> are considered programmer error and result in
     * an {@link IllegalStateException}.</p>
     *
     * <p>If the session is already closed, this is a no-op.</p>
     *
     * @param sess The session to close.
     */
    public void closeSession(CryptoSession sess) {
        synchronized (lock) {
            errorIfClosed();
            sessions.remove(sess.id);
            sess.close();
        }
    }

    /**
     * Close all open sessions.
     *
     * @see #closeSession
     */
    public void closeAllSessions() {
        synchronized (lock) {
            errorIfClosed();
            for (CryptoSession s : sessions.values()) {
                s.close();
            }
            sessions.clear();
        }
    }

    /**
     * Delete a session.
     *
     * If the session is currently loaded, it is automatically closed before
     * being deleted.
     *
     * <p>Note: After a session has been deleted, further messages received from
     * the peer can no longer be decrypted. </p>
     *
     * @param sid The ID of the session to delete.
     */
    public void deleteSession(String sid) throws CryptoException {
        synchronized (lock) {
            errorIfClosed();
            CryptoSession sess = sessions.get(sid);
            if (sess != null) {
                sessions.remove(sid);
                sess.close();
            }
            jniDeleteSession(this.ptr, sid);
        }
    }

    /**
     * Close the <tt>CryptoBox</tt>.
     *
     * <p>Note: After a box has been closed, any operations other than
     * <tt>close</tt> are considered programmer error and result in
     * an {@link IllegalStateException}.</p>
     *
     * <p>If the box is already closed, this is a no-op.</p>
     */
    public void close() {
        synchronized (lock) {
            if (isClosed()) {
                return;
            }
            closeAllSessions();
            jniClose(this.ptr);
            ptr = 0;
        }
    }

    public boolean isClosed() {
        synchronized (lock) {
            return ptr == 0;
        }
    }

    private void errorIfClosed() {
        if (isClosed()) {
            throw new IllegalStateException("Invalid operation on a closed CryptoBox.");
        }
    }

    @Override protected void finalize() throws Throwable {
        close();
    }

    private native static CryptoBox jniOpen(String dir) throws CryptoException;
    private native static CryptoBox jniOpenWith(String dir, byte[] id, int mode) throws CryptoException;
    private native static PreKey jniNewLastPreKey(long ptr) throws CryptoException;
    private native static PreKey[] jniNewPreKeys(long ptr, int start, int num) throws CryptoException;
    private native static byte[] jniGetLocalFingerprint(long ptr) throws CryptoException;
    private native static CryptoSession jniInitSessionFromPreKey(long ptr, String sid, byte[] prekey) throws CryptoException;
    private native static SessionMessage jniInitSessionFromMessage(long ptr, String sid, byte[] message) throws CryptoException;
    private native static CryptoSession jniLoadSession(long ptr, String sid) throws CryptoException;
    private native static void jniDeleteSession(long ptr, String sid) throws CryptoException;
    private native static byte[] jniCopyIdentity(long ptr) throws CryptoException;
    private native static void jniClose(long ptr);
}
