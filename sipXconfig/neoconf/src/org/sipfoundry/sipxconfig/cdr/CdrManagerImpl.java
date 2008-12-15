/*
 * 
 * 
 * Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.  
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 * 
 * $
 */
package org.sipfoundry.sipxconfig.cdr;

import java.io.IOException;
import java.io.Writer;
import java.net.MalformedURLException;
import java.net.URL;
import java.rmi.RemoteException;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Timestamp;
import java.text.Format;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Date;
import java.util.List;
import java.util.TimeZone;

import javax.xml.rpc.ServiceException;

import org.apache.commons.lang.StringUtils;
import org.apache.commons.lang.time.DateFormatUtils;
import org.apache.commons.lang.time.DateUtils;
import org.sipfoundry.sipxconfig.cdr.Cdr.Termination;
import org.sipfoundry.sipxconfig.common.User;
import org.sipfoundry.sipxconfig.common.UserException;
import org.sipfoundry.sipxconfig.service.SipxCallResolverService;
import org.sipfoundry.sipxconfig.service.SipxServiceManager;
import org.springframework.dao.DataAccessException;
import org.springframework.dao.support.DataAccessUtils;
import org.springframework.jdbc.core.PreparedStatementCreator;
import org.springframework.jdbc.core.RowCallbackHandler;
import org.springframework.jdbc.core.RowMapper;
import org.springframework.jdbc.core.SingleColumnRowMapper;
import org.springframework.jdbc.core.support.JdbcDaoSupport;

public class CdrManagerImpl extends JdbcDaoSupport implements CdrManager {
    static final String CALL_ID = "call_id";
    static final String CALLEE_AOR = "callee_aor";
    static final String TERMINATION = "termination";
    static final String FAILURE_STATUS = "failure_status";
    static final String END_TIME = "end_time";
    static final String CONNECT_TIME = "connect_time";
    static final String START_TIME = "start_time";
    static final String CALLER_AOR = "caller_aor";

    private int m_csvLimit;
    private int m_jsonLimit;

    /**
     * CDRs database at the moment is using 'timestamp' type to store UTC time. Postgres
     * 'timestamp' does not store any time zone information and JDBC driver for postgres would
     * interpret as local time. We pass TimeZone explicitely to force interpreting zoneless
     * timestamp as UTC timestamps.
     */
    private TimeZone m_tz = DateUtils.UTC_TIME_ZONE;
    private CdrServiceProvider m_cdrServiceProvider;
    private SipxServiceManager m_sipxServiceManager;

    public List<Cdr> getCdrs(Date from, Date to, User user) {
        return getCdrs(from, to, new CdrSearch(), user);
    }

    public List<Cdr> getCdrs(Date from, Date to, CdrSearch search, User user) {
        return getCdrs(from, to, search, user, 0, 0);
    }

    public List<Cdr> getCdrs(Date from, Date to, CdrSearch search, User user, int limit, int offset) {
        CdrsStatementCreator psc = new SelectAll(from, to, search, user, m_tz, limit, offset);
        CdrsResultReader resultReader = new CdrsResultReader(m_tz);
        getJdbcTemplate().query(psc, resultReader);
        return resultReader.getResults();
    }

    public void dumpCdrs(Writer writer, Date from, Date to, CdrSearch search, User user) throws IOException {
        ColumnInfoFactory columnInforFactory = new DefaultColumnInfoFactory(m_tz);
        CdrsWriter resultReader = new CdrsCsvWriter(writer, columnInforFactory);
        dump(resultReader, from, to, search, user, m_csvLimit);
    }

    public void dumpCdrsJson(Writer out) throws IOException {
        DefaultColumnInfoFactory columnInforFactory = new DefaultColumnInfoFactory(m_tz);
        columnInforFactory.setAorFormat(CdrsJsonWriter.AOR_FORMAT);
        CdrsWriter resultReader = new CdrsJsonWriter(out, columnInforFactory);
        // if we cannot see all the result - get only the latest
        CdrSearch cdrSearch = new CdrSearch();
        cdrSearch.setOrder(START_TIME, false);
        dump(resultReader, null, null, cdrSearch, null, m_jsonLimit);
    }

    /**
     * Current implementation only dumps at most m_csvLimit CDRs. This limitation is necessary due
     * to limitations of URLConnection used to download exported data to the client system.
     * 
     * See: http://bugs.sun.com/bugdatabase/view_bug.do?bug_id=4212479
     * http://bugs.sun.com/bugdatabase/view_bug.do?bug_id=5026745
     * 
     * If we had direct access to that connection we could try calling "setChunkedStreamingMode"
     * on it.
     */
    private void dump(CdrsWriter resultReader, Date from, Date to, CdrSearch search, User user, int limit)
        throws IOException {
        PreparedStatementCreator psc = new SelectAll(from, to, search, user, m_tz, limit, 0);
        try {
            resultReader.writeHeader();
            getJdbcTemplate().query(psc, resultReader);
            resultReader.writeFooter();
        } catch (DataAccessException e) {
            // unwrap IOException that might happen during reading DB
            if (e.getCause() instanceof IOException) {
                throw (IOException) e.getCause();
            }
            throw e;
        }
    }

    public int getCdrCount(Date from, Date to, CdrSearch search, User user) {
        CdrsStatementCreator psc = new SelectCount(from, to, search, user, m_tz);
        RowMapper rowMapper = new SingleColumnRowMapper(Integer.class);
        List results = getJdbcTemplate().query(psc, rowMapper);
        return (Integer) DataAccessUtils.requiredUniqueResult(results);
    }

    public List<Cdr> getActiveCalls() {
        try {
            CdrService cdrService = getCdrService();
            ActiveCall[] activeCalls = cdrService.getActiveCalls();
            List<Cdr> cdrs = new ArrayList<Cdr>(activeCalls.length);
            for (ActiveCall call : activeCalls) {
                ActiveCallCdr cdr = new ActiveCallCdr();
                cdr.setCallerAor(call.getFrom());
                cdr.setCalleeAor(call.getTo());
                cdr.setStartTime(call.getStart_time().getTime());
                cdr.setDuration(call.getDuration());
                cdrs.add(cdr);
            }
            return cdrs;
        } catch (RemoteException e) {
            throw new UserException(e);
        }
    }

    public CdrService getCdrService() {
        try {
            URL url = new URL("http", getCdrAgentAddress(), getCdrAgentPort(), StringUtils.EMPTY);
            return m_cdrServiceProvider.getCdrService(url);
        } catch (ServiceException e) {
            throw new UserException(e);
        } catch (MalformedURLException e) {
            throw new UserException(e);
        }
    }

    public void setCdrServiceProvider(CdrServiceProvider cdrServiceProvider) {
        m_cdrServiceProvider = cdrServiceProvider;
    }

    private int getCdrAgentPort() {
        return getSipxCallResolverService().getAgentPort();
    }

    private String getCdrAgentAddress() {
        return getSipxCallResolverService().getAgentAddress();
    }

    private SipxCallResolverService getSipxCallResolverService() {
        SipxCallResolverService service =
            (SipxCallResolverService) m_sipxServiceManager.getServiceByBeanId(SipxCallResolverService.BEAN_ID);
        return service;
    }

    public void setSipxServiceManager(SipxServiceManager sipxServiceManager) {
        m_sipxServiceManager = sipxServiceManager;
    }

    public void setCsvLimit(int csvLimit) {
        m_csvLimit = csvLimit;
    }

    public void setJsonLimit(int jsonLimit) {
        m_jsonLimit = jsonLimit;
    }

    abstract static class CdrsStatementCreator implements PreparedStatementCreator {
        private static final String FROM = " FROM cdrs WHERE (? <= start_time) AND (start_time <= ?)";
        private static final String LIMIT = " LIMIT ? OFFSET ?";

        private Timestamp m_from;
        private Timestamp m_to;
        private CdrSearch m_search;
        private CdrSearch m_forUser;
        private int m_limit;
        private int m_offset;
        private Calendar m_calendar;

        public CdrsStatementCreator(Date from, Date to, CdrSearch search, User user, TimeZone tz) {
            this(from, to, search, user, tz, 0, 0);
        }

        public CdrsStatementCreator(Date from, Date to, CdrSearch search, User user, TimeZone tz, int limit,
                int offset) {
            m_calendar = Calendar.getInstance(tz);
            long fromMillis = from != null ? from.getTime() : 0;
            m_from = new Timestamp(fromMillis);
            long toMillis = to != null ? to.getTime() : System.currentTimeMillis();
            m_to = new Timestamp(toMillis);
            m_search = search;
            m_limit = limit;
            m_offset = offset;
            if (user != null) {
                m_forUser = new CdrSearch();
                m_forUser.setMode(CdrSearch.Mode.ANY);
                m_forUser.setTerm(user.getUserName());
            }
        }

        public PreparedStatement createPreparedStatement(Connection con) throws SQLException {
            StringBuilder sql = new StringBuilder(getSelectSql());
            sql.append(FROM);
            m_search.appendGetSql(sql);
            if (m_forUser != null) {
                m_forUser.appendGetSql(sql);
            }
            appendOrderBySql(sql);
            if (m_limit > 0) {
                sql.append(LIMIT);
            }
            PreparedStatement ps = con.prepareStatement(sql.toString());
            ps.setTimestamp(1, m_from, m_calendar);
            ps.setTimestamp(2, m_to, m_calendar);
            if (m_limit > 0) {
                ps.setInt(3, m_limit);
                ps.setInt(4, m_offset);
            }
            return ps;
        }

        public abstract String getSelectSql();

        protected void appendOrderBySql(StringBuilder sql) {
            m_search.appendOrderBySql(sql);
        }
    }

    static class SelectAll extends CdrsStatementCreator {
        public SelectAll(Date from, Date to, CdrSearch search, User user, TimeZone tz, int limit, int offset) {
            super(from, to, search, user, tz, limit, offset);
        }

        public SelectAll(Date from, Date to, CdrSearch search, User user, TimeZone tz) {
            super(from, to, search, user, tz);
        }

        @Override
        public String getSelectSql() {
            return "SELECT *";
        }
    }

    static class SelectCount extends CdrsStatementCreator {

        public SelectCount(Date from, Date to, CdrSearch search, User user, TimeZone tz) {
            super(from, to, search, user, tz);
        }

        @Override
        public String getSelectSql() {
            return "SELECT COUNT(id)";
        }

        @Override
        protected void appendOrderBySql(StringBuilder sql) {
            // no ordering when selecting COUNT
        }
    }

    /**
     * Spring 2.0.4 introduced ResultSetExtractor interface, may be more of a fit for what this
     * class is trying to acheive
     */
    static class CdrsResultReader implements RowCallbackHandler {
        private List<Cdr> m_cdrs = new ArrayList<Cdr>();

        private Calendar m_calendar;

        public CdrsResultReader(TimeZone tz) {
            m_calendar = Calendar.getInstance(tz);
        }

        public List<Cdr> getResults() {
            return m_cdrs;
        }

        public void processRow(ResultSet rs) throws SQLException {
            Cdr cdr = new Cdr();
            cdr.setCalleeAor(rs.getString(CALLEE_AOR));
            cdr.setCallerAor(rs.getString(CALLER_AOR));
            Date startTime = rs.getTimestamp(START_TIME, m_calendar);
            cdr.setStartTime(startTime);
            Date connectTime = rs.getTimestamp(CONNECT_TIME, m_calendar);
            cdr.setConnectTime(connectTime);
            cdr.setEndTime(rs.getTimestamp(END_TIME, m_calendar));
            cdr.setFailureStatus(rs.getInt(FAILURE_STATUS));
            String termination = rs.getString(TERMINATION);
            cdr.setTermination(Termination.fromString(termination));
            m_cdrs.add(cdr);
        }
    }

    static class ColumnInfo {
        /** List of fields that will be exported to CDR */
        static final String[] FIELDS = {
            CALLEE_AOR, CALLER_AOR, START_TIME, CONNECT_TIME, END_TIME, FAILURE_STATUS, TERMINATION, CALL_ID
        };
        static final boolean[] TIME_FIELDS = {
            false, false, true, true, true, false, false, false
        };
        static final boolean[] AOR_FIELDS = {
            true, true, false, false, false, false, false, false
        };

        private final int m_rsIndex;
        private final int m_fieldIndex;
        private boolean m_timestamp;
        private Format m_format;
        private Calendar m_calendar;

        public ColumnInfo(ResultSet rs, int i, Calendar calendar, Format dateFormat, Format aorFormat)
            throws SQLException {
            m_fieldIndex = i;
            m_calendar = calendar;
            m_rsIndex = rs.findColumn(FIELDS[m_fieldIndex]);
            m_timestamp = TIME_FIELDS[m_fieldIndex];
            if (AOR_FIELDS[m_fieldIndex]) {
                m_format = aorFormat;
            } else if (TIME_FIELDS[m_fieldIndex]) {
                m_format = dateFormat;
            }
        }

        int getIndex() {
            return m_rsIndex;
        }

        boolean isTimestamp() {
            return m_timestamp;
        }

        public Object get(ResultSet rs) throws SQLException {
            if (!m_timestamp) {
                return rs.getString(m_rsIndex);
            }
            return rs.getTimestamp(m_rsIndex, m_calendar);
        }

        public String formatValue(ResultSet rs) throws SQLException {
            Object v = get(rs);
            if (v == null) {
                return StringUtils.EMPTY;
            }
            if (m_format == null) {
                return v.toString();
            }
            return m_format.format(v);
        }

        public String getField() {
            return FIELDS[m_fieldIndex];
        }
    }

    interface ColumnInfoFactory {
        ColumnInfo[] create(ResultSet rs) throws SQLException;
    }

    static class DefaultColumnInfoFactory implements ColumnInfoFactory {
        private Format m_dateFormat = DateFormatUtils.ISO_DATETIME_TIME_ZONE_FORMAT;
        private Format m_aorFormat;
        private Calendar m_calendar;

        public DefaultColumnInfoFactory(TimeZone tz) {
            m_calendar = Calendar.getInstance(tz);
        }

        public ColumnInfo[] create(ResultSet rs) throws SQLException {
            ColumnInfo[] fields = new ColumnInfo[ColumnInfo.FIELDS.length];
            for (int i = 0; i < fields.length; i++) {
                ColumnInfo ci = new ColumnInfo(rs, i, m_calendar, m_dateFormat, m_aorFormat);
                fields[i] = ci;
            }
            return fields;
        }

        public void setAorFormat(Format aorFormat) {
            m_aorFormat = aorFormat;
        }

        public void setDateFormat(Format dateFormat) {
            m_dateFormat = dateFormat;
        }
    }
}
