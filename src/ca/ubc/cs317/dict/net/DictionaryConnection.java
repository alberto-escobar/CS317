package ca.ubc.cs317.dict.net;

import ca.ubc.cs317.dict.model.Database;
import ca.ubc.cs317.dict.model.Definition;
import ca.ubc.cs317.dict.model.MatchingStrategy;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.net.Socket;
import java.util.*;

/**
 * Created by Jonatan on 2017-09-09. f
 */
public class DictionaryConnection {

    private static final int DEFAULT_PORT = 2628;
    private Socket dictSocket;
    private BufferedReader dictSocketInput;
    private PrintWriter dictSocketOutput;
    /** Establishes a new connection with a DICT server using an explicit host and port number, and handles initial
     * welcome messages.
     *
     * @param host Name of the host where the DICT server is running
     * @param port Port number used by the DICT server
     * @throws DictConnectionException If the host does not exist, the connection can't be established, or the messages
     * don't match their expected value.
     */
    public DictionaryConnection(String host, int port) throws DictConnectionException {
        // TODO Replace this with code that creates the requested connection DONE
        try {
            this.dictSocket = new Socket(host, port);
            this.dictSocketInput = new BufferedReader(new InputStreamReader(this.dictSocket.getInputStream()));
            this.dictSocketOutput = new PrintWriter(dictSocket.getOutputStream(), true);
            Status status = Status.readStatus(dictSocketInput);
            if (status.isNegativeReply()) {
                throw new DictConnectionException("Error connecting");
            }

        } catch(Exception e) {
            throw new DictConnectionException(e.toString());
        }
    }

    /** Establishes a new connection with a DICT server using an explicit host, with the default DICT port number, and
     * handles initial welcome messages.
     *
     * @param host Name of the host where the DICT server is running
     * @throws DictConnectionException If the host does not exist, the connection can't be established, or the messages
     * don't match their expected value.
     */
    public DictionaryConnection(String host) throws DictConnectionException {
        this(host, DEFAULT_PORT);
    }

    /** Sends the final QUIT message and closes the connection with the server. This function ignores any exception that
     * may happen while sending the message, receiving its reply, or closing the connection.
     *
     */
    public synchronized void close() {

        // TODO Add your code here DONE
        this.dictSocketOutput.println("QUIT");
        try {
            this.dictSocketInput.close();
            this.dictSocketOutput.close();
            this.dictSocket.close();
        } catch (Exception e) {
            //ignore exceptions
        }
    }

    /** Requests and retrieves all definitions for a specific word.
     *
     * @param word The word whose definition is to be retrieved.
     * @param database The database to be used to retrieve the definition. A special database may be specified,
     *                 indicating either that all regular databases should be used (database name '*'), or that only
     *                 definitions in the first database that has a definition for the word should be used
     *                 (database '!').
     * @return A collection of Definition objects containing all definitions returned by the server.
     * @throws DictConnectionException If the connection was interrupted or the messages don't match their expected value.
     */
    public synchronized Collection<Definition> getDefinitions(String word, Database database) throws DictConnectionException {
        Collection<Definition> set = new ArrayList<>();

        // TODO Add your code here

        try {
            this.dictSocketOutput.println("DEFINE" + " " + database.getName() + " \"" + word + "\"");

            Status status = Status.readStatus(this.dictSocketInput);

            if (status.getStatusCode() == 550) return set;
            if (status.getStatusCode() == 552) return set;
            if (status.getStatusCode() != 150) throw new DictConnectionException("DEFINE returned with " + status.getStatusCode() + status.getDetails());

            String details = status.getDetails();
            int numberOfDefinitions  = Integer.parseInt(DictStringParser.splitAtoms(details)[0]);

            for (int i = 0; i < numberOfDefinitions; i++) {
                status = Status.readStatus(this.dictSocketInput);
                if (status.getStatusCode() != 151) throw new DictConnectionException("DEFINE returned with " + status.getStatusCode() + status.getDetails());
                details = status.getDetails();
                String Details[] = DictStringParser.splitAtoms(details);
                Definition definition = new Definition(Details[0], Details[1]);

                String response = this.dictSocketInput.readLine();
                while (!response.equals(".")) {
                    definition.appendDefinition(response);
                    response = this.dictSocketInput.readLine();
                }
                set.add(definition);
            }

            status  = Status.readStatus(this.dictSocketInput);
            if (status.getStatusCode() != 250) throw new DictConnectionException("not 250, go this " + status.getDetails() + status.getDetails());
        }
        catch (Exception e) {
            e.printStackTrace();
            throw new DictConnectionException("Error in getDefinitions: " + e);
        }
        return set;
    }

    /** Requests and retrieves a list of matches for a specific word pattern.
     *
     * @param word     The word whose definition is to be retrieved.
     * @param strategy The strategy to be used to retrieve the list of matches (e.g., prefix, exact).
     * @param database The database to be used to retrieve the definition. A special database may be specified,
     *                 indicating either that all regular databases should be used (database name '*'), or that only
     *                 matches in the first database that has a match for the word should be used (database '!').
     * @return A set of word matches returned by the server.
     * @throws DictConnectionException If the connection was interrupted or the messages don't match their expected value.
     */
    public synchronized Set<String> getMatchList(String word, MatchingStrategy strategy, Database database) throws DictConnectionException {
        Set<String> set = new LinkedHashSet<>();

        // TODO Add your code here DONE
        try {
            dictSocketOutput.println("MATCH " + database.getName() + " " + strategy.getName() + " \"" + word + "\"");

            Status status = Status.readStatus(dictSocketInput);

            if (status.getStatusCode() == 550) return set;
            if (status.getStatusCode() == 551) return set;
            if (status.getStatusCode() == 552) return set;
            if (status.getStatusCode() != 152) throw new DictConnectionException("MATCH returned with " + status.getStatusCode() + status.getDetails());

            String response = dictSocketInput.readLine();
            while (!response.equals(".")) {
                String string[] = DictStringParser.splitAtoms(response);
                set.add(string[1]);
                response = dictSocketInput.readLine();
            }

            status  = Status.readStatus(this.dictSocketInput);
            if (status.getStatusCode() != 250) throw new DictConnectionException("not 250, go this " + status.getDetails() + status.getDetails());

        } catch (Exception e) {
            throw new DictConnectionException("error in getMatchList: " + e);
        }
        return set;
    }

    /** Requests and retrieves a map of database name to an equivalent database object for all valid databases used in the server.
     *
     * @return A map of Database objects supported by the server.
     * @throws DictConnectionException If the connection was interrupted or the messages don't match their expected value.
     */
    public synchronized Map<String, Database> getDatabaseList() throws DictConnectionException {
        Map<String, Database> databaseMap = new HashMap<>();

        // TODO Add your code here DONE
        try {
            dictSocketOutput.println("SHOW DB");
            // read response
            Status status = Status.readStatus(this.dictSocketInput);
            if (status.getStatusCode() == 554) return databaseMap;
            if (status.getStatusCode() != 110) throw new DictConnectionException("SHOW DB returned with " + status.getStatusCode() + status.getDetails());

            String response = dictSocketInput.readLine();
            while (!response.equals(".")) {
                String Details[] = DictStringParser.splitAtoms(response);
                Database database = new Database(Details[0], Details[1]);
                databaseMap.put(Details[0], database);
                response = dictSocketInput.readLine();
            }

            status  = Status.readStatus(this.dictSocketInput);
            if (status.getStatusCode() != 250) throw new DictConnectionException("not 250, go this " + status.getDetails());

        } catch (Exception e) {
            throw new DictConnectionException("error in getDatabaseList: " + e);
        }
        return databaseMap;
    }

    /** Requests and retrieves a list of all valid matching strategies supported by the server.
     *
     * @return A set of MatchingStrategy objects supported by the server.
     * @throws DictConnectionException If the connection was interrupted or the messages don't match their expected value.
     */
    public synchronized Set<MatchingStrategy> getStrategyList() throws DictConnectionException {
        Set<MatchingStrategy> set = new LinkedHashSet<>();

        // TODO Add your code here DONE
        try {
            this.dictSocketOutput.println("SHOW STRAT");
            Status status = Status.readStatus(dictSocketInput);
            if (status.getStatusCode() == 555) return set;
            if (status.getStatusCode() != 111) throw new DictConnectionException("SHOW STRAT returned with " + status.getStatusCode() + status.getDetails());
            String response = dictSocketInput.readLine();
            while (!response.equals(".")) {
                String strategy[] = DictStringParser.splitAtoms(response);
                MatchingStrategy ms = new MatchingStrategy(strategy[0], strategy[1]);
                set.add(ms);
                response = dictSocketInput.readLine();
            }

            status  = Status.readStatus(this.dictSocketInput);
            if (status.getStatusCode() != 250) throw new DictConnectionException("not 250, go this " + status.getDetails());
        } catch (Exception e) {
            throw new DictConnectionException("Error in getStrategyList: \n" + e.getCause());
        }
        return set;
    }

    /** Requests and retrieves detailed information about the currently selected database.
     *
     * @return A string containing the information returned by the server in response to a "SHOW INFO <db>" command.
     * @throws DictConnectionException If the connection was interrupted or the messages don't match their expected value.
     */
    public synchronized String getDatabaseInfo(Database d) throws DictConnectionException {
	StringBuilder sb = new StringBuilder();

        // TODO Add your code here DONE
        try {
            if (d.getName().equals("*")) return sb.toString();
            if (d.getName().equals("!")) return sb.toString();
            this.dictSocketOutput.println("SHOW INFO" + " " + d.getName());
            Status status = Status.readStatus(dictSocketInput);
            if (status.getStatusCode() == 550) throw new DictConnectionException("database does not exist");
            String response = dictSocketInput.readLine();
            while (!response.equals(".")) {
                sb.append(response);
                response = dictSocketInput.readLine();
            }
            status  = Status.readStatus(this.dictSocketInput);
            if (status.getStatusCode() != 250) throw new DictConnectionException("not 250, go this " + status.getDetails());
        } catch (Exception e) {
            throw new DictConnectionException("Error in getDatabaseInfo: " + e);
        }
        return sb.toString();
    }
}
