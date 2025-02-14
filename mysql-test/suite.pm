package My::Suite::Main;
use My::Platform;

@ISA = qw(My::Suite);

sub skip_combinations {
  my @combinations;

  # disable innodb/xtradb combinatons for configurations that were not built
  push @combinations, 'innodb_plugin' unless $ENV{HA_INNODB_SO};

  push @combinations, qw(xtradb innodb) unless $::mysqld_variables{'innodb'} eq "ON";

  # unconditionally, for now in 10.2. Later it could check for xtradb I_S plugins
  push @combinations, 'xtradb';

  # XtraDB is RECOMPILE_FOR_EMBEDDED, ha_xtradb.so cannot work with embedded server
  push @combinations, 'xtradb_plugin' if not $ENV{HA_XTRADB_SO}
                                          or $::opt_embedded_server;

  my %skip = ( 'include/have_innodb.combinations' => [ @combinations ],
               'include/have_xtradb.combinations' => [ @combinations ]);

  $skip{'include/innodb_encrypt_log.combinations'} = [ 'crypt' ]
                unless $ENV{DEBUG_KEY_MANAGEMENT_SO};

  # don't run tests for the wrong platform
  $skip{'include/platform.combinations'} = [ (IS_WINDOWS) ? 'unix' : 'win' ];

  $skip{'include/maybe_debug.combinations'} =
    [ defined $::mysqld_variables{'debug-dbug'} ? 'release' : 'debug' ];

  # and for the wrong word size
  # check for exact values, in case the default changes to be small everywhere
  my $longsysvar= $::mysqld_variables{'max-binlog-stmt-cache-size'};
  my %val_map= (
    '4294963200' => '64bit', # remember, it shows *what configuration to skip*
    '18446744073709547520' => '32bit'
  );
  die "unknown value max-binlog-stmt-cache-size=$longsysvar" unless $val_map{$longsysvar};
  $skip{'include/word_size.combinations'} = [ $val_map{$longsysvar} ];

  # as a special case, disable certain include files as a whole
  $skip{'include/not_embedded.inc'} = 'Not run for embedded server'
             if $::opt_embedded_server;

  $skip{'include/have_debug.inc'} = 'Requires debug build'
             unless defined $::mysqld_variables{'debug-dbug'};

  $skip{'include/have_ssl_communication.inc'} =
  $skip{'include/have_ssl_crypto_functs.inc'} = 'Requires SSL'
             unless defined $::mysqld_variables{'ssl-ca'};

  $skip{'include/have_example_plugin.inc'} = 'Need example plugin'
             unless $ENV{HA_EXAMPLE_SO};

  $skip{'include/not_windows.inc'} = 'Requires not Windows' if IS_WINDOWS;

  $skip{'t/plugin_loaderr.test'} = 'needs compiled-in innodb'
            unless $::mysqld_variables{'innodb'} eq "ON";

  $skip{'include/have_mariabackup.inc'} = 'Need mariabackup'
            unless ::have_mariabackup();

  $skip{'include/have_mariabackup.inc'} = 'Need socket statistics utility'
            unless IS_WINDOWS || ! ::have_wsrep() || ::which("lsof") || ::which("sockstat") || ::which("ss");

  $skip{'include/have_mariabackup.inc'} = 'Need socat or nc'
            unless IS_WINDOWS || $ENV{MTR_GALERA_TFMT};

  $skip{'include/have_xtrabackup.inc'} = 'Need innobackupex'
            unless IS_WINDOWS || ::which("innobackupex");

  $skip{'include/have_xtrabackup.inc'} = 'Need socat or nc'
            unless IS_WINDOWS || $ENV{MTR_GALERA_TFMT};

  $skip{'include/have_garbd.inc'} = 'Need garbd'
            unless ::have_garbd();

  $skip{'include/have_file_key_management.inc'} = 'Needs file_key_management plugin'
            unless $ENV{FILE_KEY_MANAGEMENT_SO};

  # disable tests that use ipv6, if unsupported
  sub ipv6_ok() {
    use Socket;
    return 0 unless socket my $sock, PF_INET6, SOCK_STREAM, getprotobyname('tcp');
    $!="";
    # eval{}, if there's no Socket::sockaddr_in6 at all, old Perl installation <5.14
    eval { bind $sock, sockaddr_in6($::baseport, Socket::IN6ADDR_LOOPBACK) };
    return $@ eq "" && $! eq ""
  }
  $skip{'include/check_ipv6.inc'} = 'No IPv6' unless ipv6_ok();

  $skip{'t/openssl_6975.test'} = 'no or wrong openssl version'
    unless $::mysqld_variables{'version-ssl-library'} =~ /OpenSSL (\S+)/
       and $1 ge "1.0.1d" and $1 lt "1.1.1";

  $skip{'t/ssl_7937.combinations'} = [ 'x509v3' ]
    unless $::mysqld_variables{'version-ssl-library'} =~ /OpenSSL (\S+)/
       and $1 ge "1.0.2";

  $skip{'t/ssl_verify_ip.test'} = 'x509v3 support required'
    unless $::mysqld_variables{'version-ssl-library'} =~ /OpenSSL (\S+)/
       and $1 ge "1.0.2";

  %skip;
}

bless { };
